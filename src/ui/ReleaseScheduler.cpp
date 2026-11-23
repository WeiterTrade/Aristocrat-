#include "ReleaseScheduler.h"
#include "ReleaseManager.h"
#include "Widgets.h"
#include "core/I18n.h"
#include "core/Release.h"
#include "core/Tools.h"
#include "CloudUpload.h"

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
#include <QCheckBox>
#include <QComboBox>
#include <QTemporaryDir>
#include <QProcess>
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
    m_timer.setInterval(15000);   // и наблюдение за видео текущего релиза
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

QString ReleaseScheduler::target()
{
    QSettings st;
    QString t = st.value(QStringLiteral("aeSched/target")).toString();
    if (t.isEmpty()) {   // по умолчанию — облако, подключённое во «Выгрузить»
        const QString p = st.value(QStringLiteral("upload/provider")).toString();
        t = (p.isEmpty() || p == QLatin1String("local")) ? QStringLiteral("local") : p;
    }
    return t;
}

QString ReleaseScheduler::remoteRoot()
{
    return QSettings().value(QStringLiteral("aeSched/remoteRoot"), QStringLiteral("XXX")).toString();
}

bool ReleaseScheduler::deleteLocal()
{
    return QSettings().value(QStringLiteral("aeSched/deleteLocal"), true).toBool();
}

void ReleaseScheduler::setTarget(const QString &t) { QSettings().setValue(QStringLiteral("aeSched/target"), t); }
void ReleaseScheduler::setRemoteRoot(const QString &r) { QSettings().setValue(QStringLiteral("aeSched/remoteRoot"), r.trimmed().remove(QRegularExpression(QStringLiteral("^/+|/+$")))); }
void ReleaseScheduler::setDeleteLocal(bool on) { QSettings().setValue(QStringLiteral("aeSched/deleteLocal"), on); }

void ReleaseScheduler::setLibraryDir(const QString &d) { QSettings().setValue(QStringLiteral("aeSched/libraryDir"), QDir::fromNativeSeparators(d)); }
void ReleaseScheduler::setWorkDir(const QString &d) { QSettings().setValue(QStringLiteral("aeSched/workDir"), QDir::fromNativeSeparators(d)); }

QString ReleaseScheduler::stateName(const SchedItem &it)
{
    switch (it.state) {
    case SchedItem::Waiting: return T("ждёт запуска");
    case SchedItem::Scheduled: return T("запуск в %1").arg(it.when.toString(QStringLiteral("dd.MM HH:mm")));
    case SchedItem::Running: return T("собираю данные и создаю релиз…");
    case SchedItem::Video: return it.message.isEmpty() ? T("видео: загрузка и обработка…") : T("видео: %1").arg(it.message);
    case SchedItem::Moving: return it.message.isEmpty() ? T("переношу в библиотеку…") : it.message;
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

// Видео релиза: где оно сейчас (загрузка / обработка). Пропало отовсюду — повторить загрузку, потом — ошибка.
void ReleaseScheduler::watch(SchedItem &it)
{
    const QString c = convertStatus ? convertStatus(it.dir) : QString();
    const QString d = (downloadStatus && !it.dlUrl.isEmpty()) ? downloadStatus(it.dlUrl) : QString();
    const QString now = !c.isEmpty() ? c : d;
    if (!now.isEmpty() && now != T("загрузка не удалась")) {
        it.idle = 0;
        if (it.message != now) {
            it.message = now;
            emit changed();
        }
        return;
    }
    if (++it.idle < 8)   // ~2 минуты: между загрузкой и обработкой бывает пауза
        return;
    it.idle = 0;
    if (it.retries < 2 && !it.dlUrl.isEmpty() && requestDownload) {
        ++it.retries;
        it.message = T("видео не найдено в загрузках — повторяю загрузку (%1 из 2)").arg(it.retries);
        save();
        emit changed();
        requestDownload(it.dlUrl);
        return;
    }
    finishItem(it.id, false, T("видео не скачалось — релиз (без видео) в рабочей папке; «Запустить сейчас» — повторить"));
}

void ReleaseScheduler::tick()
{
    if (m_current) {
        if (SchedItem *it = find(m_current))
            if (it->state == SchedItem::Video)
                watch(*it);
        return;   // по одному релизу
    }
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
    connect(d, &ReleaseManagerDialog::downloadRequested, this, [this, id](const QString &page) {
        if (SchedItem *x = find(id)) {
            x->dlUrl = page;
            x->retries = 0;
            x->idle = 0;
        }
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
    if (it->retries < 2 && !it->dlUrl.isEmpty() && requestDownload) {   // например, обновили модули — ещё попытка
        ++it->retries;
        it->idle = 0;
        it->message = T("%1 — повторю через минуту (%2 из 2)").arg(reason).arg(it->retries);
        save();
        emit changed();
        const int id = it->id;
        QTimer::singleShot(60000, this, [this, id] {
            SchedItem *x = find(id);
            if (x && x->state == SchedItem::Video && requestDownload)
                requestDownload(x->dlUrl);
        });
        return;
    }
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

static QString nfoValue(const QString &dir, const QString &tag)
{
    for (const QString &nfo : QDir(dir).entryList({QStringLiteral("*.nfo")}, QDir::Files)) {
        QFile f(QDir(dir).filePath(nfo));
        if (!f.open(QIODevice::ReadOnly))
            return {};
        const QRegularExpression re(QStringLiteral("<%1>([^<]+)</%1>").arg(tag));
        QString v = re.match(QString::fromUtf8(f.readAll())).captured(1).trimmed();
        return v.replace(QStringLiteral("&amp;"), QStringLiteral("&")).replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    }
    return {};
}

// В NFO релиза — название коллекции, как у существующей папки серии
static void setNfoSet(const QString &dir, const QString &setName)
{
    for (const QString &nfo : QDir(dir).entryList({QStringLiteral("*.nfo")}, QDir::Files)) {
        QFile f(QDir(dir).filePath(nfo));
        if (!f.open(QIODevice::ReadOnly))
            return;
        QString x = QString::fromUtf8(f.readAll());
        f.close();
        static const QRegularExpression re(QStringLiteral("(<set>\\s*<name>)([^<]*)(</name>)"));
        x.replace(re, QStringLiteral("\\1") + setName.toHtmlEscaped() + QStringLiteral("\\3"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(x.toUtf8());
        return;
    }
}

void ReleaseScheduler::moveToLibrary(int id)
{
    SchedItem *it = find(id);
    if (!it)
        return;
    const QString library = libraryDir(), work = workDir();
    QString tgt = target();
    if (tgt != QLatin1String("local")
        && !QSettings(CloudUploadPage::configFile(), QSettings::IniFormat).childGroups().contains(QStringLiteral("aristo-") + tgt))
        tgt = QStringLiteral("local");   // облако не подключено — в папку на диске
    const QString studio = Release::cleanStudio(nfoValue(it->dir, QStringLiteral("studio")));
    const QString orig = nfoValue(it->dir, QStringLiteral("originaltitle"));

    // [Серия «…»/]Релиз — относительно рабочей папки
    QString rel = QDir(work).relativeFilePath(it->dir);
    if (rel.startsWith(QLatin1String("..")))
        rel = QFileInfo(it->dir).fileName();
    const bool hasLib = !library.isEmpty() && QDir(library).exists();
    const QString studioDir = hasLib ? studioFolder(library, studio) : QString();

    // Серия уже есть в библиотеке (по английским оригиналам) — та же папка и то же название коллекции
    if (rel.contains(QLatin1Char('/'))) {
        const QString seriesNow = rel.section(QLatin1Char('/'), 0, 0);
        QString existing = hasLib ? Release::findSeriesFolder(QDir(library).filePath(studioDir), orig) : QString();
        if (existing.isEmpty())
            existing = Release::findSeriesFolder(QString(), orig);
        if (!existing.isEmpty() && existing != seriesNow) {
            setNfoSet(it->dir, existing);
            rel = existing + QLatin1Char('/') + rel.section(QLatin1Char('/'), 1);
        }
        Release::rememberSeriesFolder(orig, existing.isEmpty() ? seriesNow : existing);
    }

    if (tgt != QLatin1String("local")) {   // облако — через rclone, как во «Выгрузить»
        if (hasLib) {
            uploadTo(id, studioDir, rel);
            return;
        }
        // Библиотеки на диске нет — папки студий смотрим прямо в облаке
        auto *p = new QProcess(this);
        const QString remote = QStringLiteral("aristo-%1:%2").arg(tgt, remoteRoot());
        connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, p, id, studio, rel](int, QProcess::ExitStatus) {
            p->deleteLater();
            const QStringList dirs = QString::fromUtf8(p->readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            QTemporaryDir tmp;
            for (const QString &d : dirs)
                QDir(tmp.path()).mkdir(QString(d).remove(QLatin1Char('/')).trimmed());
            uploadTo(id, studioFolder(tmp.path(), studio), rel);
        });
        p->start(Tools::rclone(), {QStringLiteral("lsf"), remote, QStringLiteral("--dirs-only"), QStringLiteral("--config"), CloudUploadPage::configFile()});
        return;
    }

    // Папка на диске
    if (!hasLib) {
        it->dest = it->dir;
        finishItem(id, true, T("библиотека не указана — релиз в рабочей папке"));
        return;
    }
    const QString dest = QDir(QDir(library).filePath(studioDir)).filePath(rel);
    it->state = SchedItem::Moving;
    it->message.clear();
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
        const QDir parent = QFileInfo(src).dir();
        if (QDir::cleanPath(parent.absolutePath()) != QDir::cleanPath(QDir(work).absolutePath()) && parent.isEmpty())
            QDir().rmdir(parent.absolutePath());
        QSettings().setValue(QStringLiteral("ae/lastRelease"), dest);
        finishItem(id, true, QString());
    });
}

// Выгрузка в облако через rclone (подключение из «Выгрузить»): Корень/Студия/[Серия «…»/]Релиз
void ReleaseScheduler::uploadTo(int id, const QString &studioDir, const QString &rel)
{
    SchedItem *it = find(id);
    if (!it)
        return;
    const QString tgt = target();
    const QString remotePath = remoteRoot() + QLatin1Char('/') + studioDir + QLatin1Char('/') + rel;
    const QString dst = QStringLiteral("aristo-%1:%2").arg(tgt, remotePath);
    it->state = SchedItem::Moving;
    it->message = T("выгрузка в облако…");
    it->dest = remotePath;
    save();
    emit changed();
    QStringList args{QStringLiteral("copy"), it->dir, dst, QStringLiteral("--stats"), QStringLiteral("2s"), QStringLiteral("--stats-one-line"),
                     QStringLiteral("--stats-log-level"), QStringLiteral("NOTICE"), QStringLiteral("--transfers"), QStringLiteral("4"),
                     QStringLiteral("--checkers"), QStringLiteral("8"), QStringLiteral("--buffer-size"), QStringLiteral("64M"),
                     QStringLiteral("--exclude"), QStringLiteral(".aristocrat-temp/**"),
                     QStringLiteral("--config"), CloudUploadPage::configFile()};
    if (tgt == QLatin1String("mailru"))
        args << QStringLiteral("--mailru-speedup-enable=false");
    else if (tgt == QLatin1String("google"))
        args << QStringLiteral("--drive-chunk-size") << QStringLiteral("128M");
    else if (tgt == QLatin1String("onedrive"))
        args << QStringLiteral("--onedrive-chunk-size") << QStringLiteral("100M");
    auto *p = new QProcess(this);
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::readyRead, this, [this, p, id] {
        static const QRegularExpression pct(QStringLiteral("(\\d+)%")), speed(QStringLiteral("([0-9.]+\\s*[KMG]i?B/s)")),
            eta(QStringLiteral("ETA\\s+([0-9hms]+)"));
        const QStringList lines = QString::fromUtf8(p->readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QString line = lines.isEmpty() ? QString() : lines.constLast();
        const auto m = pct.match(line);
        if (!m.hasMatch())
            return;
        if (SchedItem *x = find(id)) {
            x->message = T("выгрузка в облако: %1%, %2, осталось %3").arg(m.captured(1), speed.match(line).captured(1), eta.match(line).captured(1));
            emit changed();
        }
    });
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, p, id](int code, QProcess::ExitStatus st) {
        p->deleteLater();
        SchedItem *x = find(id);
        if (!x)
            return;
        if (st != QProcess::NormalExit || code != 0) {
            finishItem(id, false, T("выгрузка в облако не удалась — релиз в рабочей папке, можно выгрузить во «Выгрузить»"));
            return;
        }
        // Анкеты и изображения на сервер читают NFO и картинки релиза. Локальную копию удаляем, только если
        // тот же релиз виден через библиотеку на диске (диск облака, X:\XXX) — тогда анкеты открываются оттуда.
        const QString library = libraryDir();
        const QString mirror = library.isEmpty() ? QString()
                                                 : QDir(library).filePath(x->dest.section(QLatin1Char('/'), 1));
        if (deleteLocal() && !library.isEmpty() && QDir(library).exists()) {
            x->dest = mirror;
            QSettings().setValue(QStringLiteral("ae/lastRelease"), mirror);
            const QString src = x->dir, work = workDir();
            QDir(src).removeRecursively();
            const QDir parent = QFileInfo(src).dir();
            if (QDir::cleanPath(parent.absolutePath()) != QDir::cleanPath(QDir(work).absolutePath()) && parent.isEmpty())
                QDir().rmdir(parent.absolutePath());
        } else {
            x->dest = x->dir;
            QSettings().setValue(QStringLiteral("ae/lastRelease"), x->dir);
        }
        finishItem(id, true, QString());
    });
    p->start(Tools::rclone(), args);
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
    g->addWidget(new QLabel(T("Библиотека на диске:"), this));
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

    // Куда выгружать готовый релиз: облако через rclone (как во «Выгрузить») или папка на диске
    auto *u = new QHBoxLayout;
    u->addWidget(new QLabel(T("Выгружать в:"), this));
    m_target = new QComboBox(this);
    {
        const QSettings conf(CloudUploadPage::configFile(), QSettings::IniFormat);
        const QStringList groups = conf.childGroups();
        const QList<QPair<QString, QString>> clouds = {{QStringLiteral("mailru"), T("Облако Mail.ru")},
                                                       {QStringLiteral("yandex"), T("Яндекс Диск")},
                                                       {QStringLiteral("google"), T("Google Диск")},
                                                       {QStringLiteral("onedrive"), QStringLiteral("OneDrive")}};
        for (const auto &c : clouds)
            if (groups.contains(QStringLiteral("aristo-") + c.first))
                m_target->addItem(T("%1 (через утилиту, как во «Выгрузить»)").arg(c.second), c.first);
        m_target->addItem(T("Папка на диске (библиотека)"), QStringLiteral("local"));
        const int i = m_target->findData(ReleaseScheduler::target());
        m_target->setCurrentIndex(i >= 0 ? i : 0);
        if (m_target->count() == 1)
            m_target->setToolTip(T("Облако не подключено — подключите его на вкладке «Выгрузить»"));
    }
    u->addWidget(m_target, 1);
    u->addSpacing(12);
    u->addWidget(new QLabel(T("Папка в облаке:"), this));
    m_root = new QLineEdit(ReleaseScheduler::remoteRoot(), this);
    m_root->setPlaceholderText(QStringLiteral("XXX"));
    m_root->setMaximumWidth(160);
    u->addWidget(m_root);
    m_delete = new QCheckBox(T("Удалять локальную копию после выгрузки"), this);
    m_delete->setChecked(ReleaseScheduler::deleteLocal());
    u->addWidget(m_delete);
    root->addLayout(u);
    auto syncTarget = [this] {
        const bool cloud = m_target->currentData().toString() != QLatin1String("local");
        m_root->setEnabled(cloud);
        m_delete->setEnabled(cloud);
    };
    syncTarget();
    connect(m_target, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, syncTarget] {
        ReleaseScheduler::setTarget(m_target->currentData().toString());
        syncTarget();
    });
    connect(m_root, &QLineEdit::editingFinished, this, [this] { ReleaseScheduler::setRemoteRoot(m_root->text()); });
    connect(m_delete, &QCheckBox::toggled, this, [](bool on) { ReleaseScheduler::setDeleteLocal(on); });
    m_lib->setToolTip(T("По этой папке ищутся существующие студии и серии; при выгрузке в облако туда ничего не копируется"));

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
        ReleaseScheduler::setTarget(m_target->currentData().toString());
        ReleaseScheduler::setRemoteRoot(m_root->text());
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
