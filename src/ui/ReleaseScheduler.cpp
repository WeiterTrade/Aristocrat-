#include "ReleaseScheduler.h"
#include "ReleaseManager.h"
#include "Widgets.h"
#include "core/I18n.h"
#include "core/Release.h"

#include <QApplication>
#include <QDateTimeEdit>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtMath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

// ======================= Сигнал «готово» =======================

namespace DoneSound {

static QByteArray melody()
{
    // Короткая мелодия, как у стиральной машины по окончании стирки
    const int rate = 22050;
    struct Note { double f; double len; };
    const Note notes[] = {{1046.5, 0.18}, {1318.5, 0.18}, {1568.0, 0.18}, {2093.0, 0.36}, {0, 0.10},
                          {1568.0, 0.18}, {1760.0, 0.18}, {2093.0, 0.55}};
    QByteArray pcm;
    for (const Note &n : notes) {
        const int count = int(n.len * rate);
        for (int i = 0; i < count; ++i) {
            double v = 0;
            if (n.f > 0) {
                const double t = double(i) / rate;
                const double env = qMin(1.0, t / 0.01) * qExp(-3.2 * t / n.len);
                v = env * (0.75 * qSin(2 * M_PI * n.f * t) + 0.25 * qSin(4 * M_PI * n.f * t));
            }
            const qint16 s = qint16(qBound(-1.0, v * 0.55, 1.0) * 32767);
            pcm.append(char(s & 0xff)).append(char((s >> 8) & 0xff));
        }
    }
    QByteArray wav;
    auto u32 = [&wav](quint32 x) { for (int i = 0; i < 4; ++i) wav.append(char((x >> (8 * i)) & 0xff)); };
    auto u16 = [&wav](quint16 x) { wav.append(char(x & 0xff)).append(char(x >> 8)); };
    wav.append("RIFF"); u32(36 + pcm.size()); wav.append("WAVEfmt ");
    u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    wav.append("data"); u32(pcm.size()); wav.append(pcm);
    return wav;
}

void play()
{
#ifdef Q_OS_WIN
    static const QByteArray wav = melody();   // буфер должен жить, пока звучит
    PlaySoundW(reinterpret_cast<LPCWSTR>(wav.constData()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#else
    QApplication::beep();
#endif
}

} // namespace DoneSound

// ======================= Планировщик =======================

ReleaseScheduler *ReleaseScheduler::instance()
{
    static ReleaseScheduler *s = new ReleaseScheduler(qApp);
    return s;
}

ReleaseScheduler::ReleaseScheduler(QObject *parent) : QObject(parent)
{
    load();
    m_timer.setInterval(15000);
    connect(&m_timer, &QTimer::timeout, this, &ReleaseScheduler::tick);
    m_timer.start();
}

QString ReleaseScheduler::libraryDir()
{
    QSettings st;
    QString d = st.value(QStringLiteral("aeSched/libraryDir")).toString();
    if (d.isEmpty() && QDir(QStringLiteral("X:/XXX")).exists())
        d = QStringLiteral("X:/XXX");
    return d;
}

QString ReleaseScheduler::workDir()
{
    QSettings st;
    QString d = st.value(QStringLiteral("aeSched/workDir")).toString();
    if (d.isEmpty())
        d = st.value(QStringLiteral("ae/baseDir")).toString();
    if (d.isEmpty())
        d = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/Aristocrat Релизы");
    return QDir::fromNativeSeparators(d);
}

void ReleaseScheduler::setLibraryDir(const QString &d) { QSettings().setValue(QStringLiteral("aeSched/libraryDir"), QDir::fromNativeSeparators(d)); }
void ReleaseScheduler::setWorkDir(const QString &d) { QSettings().setValue(QStringLiteral("aeSched/workDir"), QDir::fromNativeSeparators(d)); }

QString ReleaseScheduler::stateName(const SchedItem &it)
{
    switch (it.state) {
    case SchedItem::Waiting: return T("ждёт запуска");
    case SchedItem::Scheduled: return T("запуск в %1").arg(it.when.toString(QStringLiteral("dd.MM HH:mm")));
    case SchedItem::Running: return T("собираю данные и создаю релиз…");
    case SchedItem::Video: return T("видео: загрузка и обработка…");
    case SchedItem::Moving: return T("переношу в библиотеку…");
    case SchedItem::Done: return T("готово");
    case SchedItem::Failed: return T("ошибка: %1").arg(it.message);
    }
    return {};
}

QStringList ReleaseScheduler::actressesOf(const QString &releaseDir)
{
    QStringList out;
    const QDir d(releaseDir);
    for (const QString &nfo : d.entryList({QStringLiteral("*.nfo")}, QDir::Files)) {
        QFile f(d.filePath(nfo));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        static const QRegularExpression re(QStringLiteral("<actor>\\s*<name>([^<]+)</name>"));
        auto it = re.globalMatch(QString::fromUtf8(f.readAll()));
        while (it.hasNext()) {
            const QString n = it.next().captured(1).trimmed();
            if (!out.contains(n))
                out << n;
        }
        break;
    }
    return out;
}

SchedItem *ReleaseScheduler::find(int id)
{
    for (SchedItem &it : m_items)
        if (it.id == id)
            return &it;
    return nullptr;
}

void ReleaseScheduler::add(const QString &urlIn)
{
    const QString url = urlIn.trimmed();
    if (url.isEmpty())
        return;
    SchedItem it;
    it.id = m_nextId++;
    it.url = url;
    m_items << it;
    save();
    emit changed();
}

void ReleaseScheduler::remove(int id)
{
    if (id == m_current)
        return;   // идёт — не трогаем
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].id == id) {
            m_items.removeAt(i);
            break;
        }
    save();
    emit changed();
}

void ReleaseScheduler::startNow()
{
    for (SchedItem &it : m_items)
        if (it.state == SchedItem::Waiting || it.state == SchedItem::Scheduled || it.state == SchedItem::Failed) {
            it.state = SchedItem::Scheduled;
            it.when = QDateTime::currentDateTime();
            it.message.clear();
        }
    save();
    emit changed();
    tick();
}

void ReleaseScheduler::startAt(const QDateTime &when)
{
    for (SchedItem &it : m_items)
        if (it.state == SchedItem::Waiting || it.state == SchedItem::Failed) {
            it.state = SchedItem::Scheduled;
            it.when = when;
            it.message.clear();
        }
    save();
    emit changed();
}

void ReleaseScheduler::tick()
{
    if (m_current)
        return;   // по одному релизу
    for (SchedItem &it : m_items)
        if (it.state == SchedItem::Scheduled && it.when <= QDateTime::currentDateTime()) {
            process(it);
            return;
        }
    // Очередь пройдена — сигнал и список для анкет
    QList<SchedItem> done;
    for (SchedItem &it : m_items)
        if ((it.state == SchedItem::Done || it.state == SchedItem::Failed) && !it.notified) {
            it.notified = true;
            done << it;
        }
    if (!done.isEmpty()) {
        save();
        emit batchFinished(done);
    }
}

void ReleaseScheduler::process(SchedItem &it)
{
    m_current = it.id;
    it.state = SchedItem::Running;
    it.message.clear();
    save();
    emit changed();
    const int id = it.id;
    auto *d = new ReleaseManagerDialog(it.url, QString(), nullptr);   // не показывается: всё делает сам
    m_dialog = d;
    connect(d, &ReleaseManagerDialog::downloadRequested, this, [this](const QString &page) {
        if (requestDownload)
            requestDownload(page);
    });
    connect(d, &ReleaseManagerDialog::autoFailed, this, [this, id, d](const QString &reason) {
        d->deleteLater();
        finishItem(id, false, reason);
    });
    connect(d, &ReleaseManagerDialog::autoCreated, this, [this, id, d](const QString &dir, bool videoPending) {
        SchedItem *it = find(id);
        if (!it)
            return;
        it->dir = dir;
        it->title = QFileInfo(dir).fileName();
        QTimer::singleShot(3000, d, &QObject::deleteLater);
        if (videoPending) {
            it->state = SchedItem::Video;
            save();
            emit changed();
            return;   // дальше — onVideoDone / onDownloadFailed
        }
        moveToLibrary(id);
    });
    d->startAuto(workDir());
}

void ReleaseScheduler::onVideoDone(const QString &dir, bool ok)
{
    SchedItem *it = find(m_current);
    if (!it || it->state != SchedItem::Video
        || QDir::cleanPath(QFileInfo(dir).absoluteFilePath()) != QDir::cleanPath(QFileInfo(it->dir).absoluteFilePath()))
        return;
    if (!ok) {
        finishItem(it->id, false, T("видео не обработалось — релиз остался в рабочей папке"));
        return;
    }
    moveToLibrary(it->id);
}

void ReleaseScheduler::onDownloadFailed(const QString &pageUrl, const QString &reason)
{
    Q_UNUSED(pageUrl)
    SchedItem *it = find(m_current);
    if (!it || it->state != SchedItem::Video)
        return;
    finishItem(it->id, false, T("видео: %1 — релиз (без видео) остался в рабочей папке").arg(reason));
}

// Папка студии в библиотеке: такая же (без учёта регистра и знаков) — иначе «Остальные Студии»
static QString studioFolder(const QString &library, const QString &studio)
{
    const QDir lib(library);
    auto norm = [](QString s) { return s.toLower().remove(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]"))); };
    const QString ns = norm(studio);
    QString other;
    for (const QString &d : lib.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString nd = norm(d);
        if (nd.contains(QStringLiteral("остальн")))
            other = d;
        if (nd.contains(QStringLiteral("файлыдляработы")) || nd.isEmpty())
            continue;
        if (!ns.isEmpty() && (nd == ns || (nd.size() >= 4 && ns.contains(nd)) || (ns.size() >= 4 && nd.contains(ns))))
            return d;
    }
    return other.isEmpty() ? QStringLiteral("Остальные Студии") : other;
}

static bool copyTree(const QString &from, const QString &to)
{
    QDir().mkpath(to);
    bool ok = true;
    QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QString rel = QDir(from).relativeFilePath(f);
        if (rel.startsWith(QLatin1String(".aristocrat-temp")))
            continue;
        const QString dst = QDir(to).filePath(rel);
        QDir().mkpath(QFileInfo(dst).absolutePath());
        QFile::remove(dst);
        ok = QFile::copy(f, dst) && ok;
    }
    return ok;
}

void ReleaseScheduler::moveToLibrary(int id)
{
    SchedItem *it = find(id);
    if (!it)
        return;
    const QString library = libraryDir(), work = workDir();
    if (library.isEmpty() || !QDir(library).exists()) {
        it->dest = it->dir;
        finishItem(id, true, T("библиотека не указана — релиз в рабочей папке"));
        return;
    }
    // Студия — из NFO
    QString studio;
    for (const QString &nfo : QDir(it->dir).entryList({QStringLiteral("*.nfo")}, QDir::Files)) {
        QFile f(QDir(it->dir).filePath(nfo));
        if (f.open(QIODevice::ReadOnly)) {
            static const QRegularExpression re(QStringLiteral("<studio>([^<]+)</studio>"));
            studio = re.match(QString::fromUtf8(f.readAll())).captured(1).trimmed();
        }
        break;
    }
    // Относительный путь сохраняется: [Серия «…»/]Релиз
    QString rel = QDir(work).relativeFilePath(it->dir);
    if (rel.startsWith(QLatin1String("..")))
        rel = QFileInfo(it->dir).fileName();
    const QString dest = QDir(QDir(library).filePath(studioFolder(library, studio))).filePath(rel);
    it->state = SchedItem::Moving;
    it->dest = dest;
    save();
    emit changed();
    const QString src = it->dir;
    auto ok = QSharedPointer<bool>::create(false);
    Ui::runAsync(this, [src, dest, ok] { *ok = copyTree(src, dest); }, [this, id, src, dest, ok, work] {
        if (!*ok) {
            finishItem(id, false, T("не удалось перенести в библиотеку — релиз остался в рабочей папке"));
            return;
        }
        QDir(src).removeRecursively();
        // пустая папка серии в рабочей папке — тоже
        const QDir parent = QFileInfo(src).dir();
        if (QDir::cleanPath(parent.absolutePath()) != QDir::cleanPath(QDir(work).absolutePath()) && parent.isEmpty())
            QDir().rmdir(parent.absolutePath());
        QSettings().setValue(QStringLiteral("ae/lastRelease"), dest);
        finishItem(id, true, QString());
    });
}

void ReleaseScheduler::finishItem(int id, bool ok, const QString &message)
{
    if (SchedItem *it = find(id)) {
        it->state = ok ? SchedItem::Done : SchedItem::Failed;
        it->message = message;
        if (ok && it->dest.isEmpty())
            it->dest = it->dir;
    }
    if (m_current == id)
        m_current = 0;
    save();
    emit changed();
    QTimer::singleShot(500, this, &ReleaseScheduler::tick);
}

void ReleaseScheduler::save()
{
    QJsonArray a;
    for (const SchedItem &it : m_items) {
        QJsonObject o;
        o["id"] = it.id;
        o["url"] = it.url;
        o["title"] = it.title;
        o["state"] = int(it.state);
        o["when"] = it.when.toString(Qt::ISODate);
        o["dir"] = it.dir;
        o["dest"] = it.dest;
        o["message"] = it.message;
        o["notified"] = it.notified;
        a << o;
    }
    QSettings().setValue(QStringLiteral("aeSched/queue"), QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact)));
}

void ReleaseScheduler::load()
{
    const QJsonArray a = QJsonDocument::fromJson(QSettings().value(QStringLiteral("aeSched/queue")).toString().toUtf8()).array();
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        SchedItem it;
        it.id = o.value("id").toInt();
        it.url = o.value("url").toString();
        it.title = o.value("title").toString();
        it.state = SchedItem::State(o.value("state").toInt());
        it.when = QDateTime::fromString(o.value("when").toString(), Qt::ISODate);
        it.dir = o.value("dir").toString();
        it.dest = o.value("dest").toString();
        it.message = o.value("message").toString();
        it.notified = o.value("notified").toBool();
        // Прерванные закрытием программы — начать заново
        if (it.state == SchedItem::Running || it.state == SchedItem::Video || it.state == SchedItem::Moving) {
            it.state = SchedItem::Failed;
            it.message = T("прервано закрытием программы — нажмите «Запустить сейчас»");
        }
        m_nextId = qMax(m_nextId, it.id + 1);
        m_items << it;
    }
}

// ======================= Вкладка «Планировщик» =======================

SchedulerPage::SchedulerPage(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    auto *hint = new QLabel(T("Вставьте ссылки на релизы Adult Empire и нажмите «Запланировать». Программа сама соберёт данные, "
                              "переведёт, сделает обложки и фан-арт, скачает видео в 720p (ниже — улучшит до 720p), вошьёт сцены и "
                              "перенесёт релиз в библиотеку: Студия (или «Остальные Студии») / Серия «…» / Релиз. В конце прозвучит сигнал — "
                              "останется заполнить анкеты актрис."), this);
    hint->setWordWrap(true);
    root->addWidget(hint);

    m_rows = new QVBoxLayout;
    m_rows->setSpacing(6);
    root->addLayout(m_rows);
    auto *plus = new QPushButton(QStringLiteral("+"), this);
    plus->setToolTip(T("Ещё одна ссылка"));
    plus->setFixedWidth(44);
    auto *plusRow = new QHBoxLayout;
    plusRow->addWidget(plus);
    plusRow->addStretch();
    root->addLayout(plusRow);
    connect(plus, &QPushButton::clicked, this, [this] { addRow(); });
    addRow();

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({T("Релиз"), T("Состояние"), T("Где")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_table, 1);

    auto *btns = new QHBoxLayout;
    auto *now = Ui::primaryButton(T("Запустить сейчас"), this);
    m_when = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(3600), this);
    m_when->setDisplayFormat(QStringLiteral("dd.MM.yyyy HH:mm"));
    m_when->setCalendarPopup(true);
    auto *later = new QPushButton(T("Запустить позже"), this);
    auto *removeBtn = new QPushButton(T("Убрать"), this);
    auto *forms = new QPushButton(T("Анкеты актрис…"), this);
    forms->setToolTip(T("Заполнить анкеты актрис выбранного готового релиза и записать на сервер"));
    btns->addWidget(now);
    btns->addSpacing(12);
    btns->addWidget(m_when);
    btns->addWidget(later);
    btns->addStretch();
    btns->addWidget(removeBtn);
    btns->addWidget(forms);
    root->addLayout(btns);

    auto *g = new QHBoxLayout;
    g->addWidget(new QLabel(T("Библиотека (куда выгружать):"), this));
    m_lib = new QLineEdit(QDir::toNativeSeparators(ReleaseScheduler::libraryDir()), this);
    m_lib->setPlaceholderText(QStringLiteral("X:\\XXX"));
    auto *libBtn = new QPushButton(T("Изменить…"), this);
    g->addWidget(m_lib, 1);
    g->addWidget(libBtn);
    g->addSpacing(12);
    g->addWidget(new QLabel(T("Рабочая папка:"), this));
    m_work = new QLineEdit(QDir::toNativeSeparators(ReleaseScheduler::workDir()), this);
    auto *workBtn = new QPushButton(T("Изменить…"), this);
    g->addWidget(m_work, 1);
    g->addWidget(workBtn);
    root->addLayout(g);
    m_state = new QLabel(this);
    m_state->setObjectName(QStringLiteral("StatusLabel"));
    m_state->setWordWrap(true);
    root->addWidget(m_state);

    connect(m_lib, &QLineEdit::editingFinished, this, [this] { ReleaseScheduler::setLibraryDir(m_lib->text().trimmed()); });
    connect(m_work, &QLineEdit::editingFinished, this, [this] { ReleaseScheduler::setWorkDir(m_work->text().trimmed()); });
    connect(libBtn, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, T("Библиотека (папка со студиями)"), m_lib->text());
        if (!d.isEmpty()) {
            m_lib->setText(QDir::toNativeSeparators(d));
            ReleaseScheduler::setLibraryDir(d);
        }
    });
    connect(workBtn, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, T("Рабочая папка"), m_work->text());
        if (!d.isEmpty()) {
            m_work->setText(QDir::toNativeSeparators(d));
            ReleaseScheduler::setWorkDir(d);
        }
    });
    auto *s = ReleaseScheduler::instance();
    connect(now, &QPushButton::clicked, this, [this, s] {
        ReleaseScheduler::setLibraryDir(m_lib->text().trimmed());
        ReleaseScheduler::setWorkDir(m_work->text().trimmed());
        s->startNow();
        m_state->setText(T("Запущено. Окно можно закрыть или свернуть — работа продолжится."));
    });
    connect(later, &QPushButton::clicked, this, [this, s] {
        ReleaseScheduler::setLibraryDir(m_lib->text().trimmed());
        ReleaseScheduler::setWorkDir(m_work->text().trimmed());
        s->startAt(m_when->dateTime());
        m_state->setText(T("Запуск в %1. Программа должна быть открыта.").arg(m_when->dateTime().toString(QStringLiteral("dd.MM.yyyy HH:mm"))));
    });
    connect(removeBtn, &QPushButton::clicked, this, [this, s] {
        const int row = m_table->currentRow();
        if (row >= 0)
            s->remove(m_table->item(row, 0)->data(Qt::UserRole).toInt());
    });
    connect(forms, &QPushButton::clicked, this, [this, s] {
        const int row = m_table->currentRow();
        if (row < 0)
            return;
        const int id = m_table->item(row, 0)->data(Qt::UserRole).toInt();
        for (const SchedItem &it : s->items())
            if (it.id == id && it.state == SchedItem::Done)
                emit openServer(it.dest);
    });
    connect(s, &ReleaseScheduler::changed, this, &SchedulerPage::refresh);
    refresh();
}

void SchedulerPage::addRow(const QString &text)
{
    auto *row = new QWidget(this);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    auto *edit = new QLineEdit(text, row);
    edit->setPlaceholderText(T("Ссылка на релиз Adult Empire"));
    auto *plan = new QPushButton(T("Запланировать"), row);
    h->addWidget(edit, 1);
    h->addWidget(plan);
    m_rows->addWidget(row);
    auto add = [this, edit, row] {
        const QString u = edit->text().trimmed();
        if (u.isEmpty())
            return;
        ReleaseScheduler::instance()->add(u);
        edit->clear();
        if (m_rows->count() > 1)
            row->deleteLater();
    };
    connect(plan, &QPushButton::clicked, this, add);
    connect(edit, &QLineEdit::returnPressed, this, add);
    edit->setFocus();
}

void SchedulerPage::refresh()
{
    const auto &items = ReleaseScheduler::instance()->items();
    m_table->setRowCount(items.size());
    for (int r = 0; r < items.size(); ++r) {
        const SchedItem &it = items[r];
        auto *a = new QTableWidgetItem(it.title.isEmpty() ? it.url : it.title);
        a->setToolTip(it.url);
        a->setData(Qt::UserRole, it.id);
        m_table->setItem(r, 0, a);
        auto *b = new QTableWidgetItem(ReleaseScheduler::stateName(it));
        b->setToolTip(b->text());
        m_table->setItem(r, 1, b);
        auto *c = new QTableWidgetItem(QDir::toNativeSeparators(it.dest.isEmpty() ? it.dir : it.dest));
        c->setToolTip(c->text());
        m_table->setItem(r, 2, c);
    }
}
