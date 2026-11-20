#include "ChaptersDialog.h"
#include "Widgets.h"
#include "core/I18n.h"
#include "core/Theme.h"
#include "core/Settings.h"
#include "core/Task.h"
#include "core/Tools.h"
#ifdef HAVE_WEBENGINE
#include "SceneTools.h"
#endif

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextStream>
#include <QMenu>
#include <QDateTime>
#include <QVBoxLayout>
#include <algorithm>

enum Col { ColStart = 0, ColEnd, ColTitle, ColOriginal };

ChaptersDialog::ChaptersDialog(const QString &file, const QString &pageUrl, QWidget *parent)
    : FramedDialog(parent, true)
{
    setWindowTitle(T("Главы (сцены)"));
    resize(1040, 660);
    auto *root = contentLayout();
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(10);

    // Видеофайл
    auto *r1 = new QHBoxLayout;
    m_file = new QLineEdit(this);
    m_file->setReadOnly(true);
    m_file->setPlaceholderText(T("Выберите видеофайл (MKV или MP4)"));
    auto *pick = new QPushButton(T("Выбрать видео…"), this);
    r1->addWidget(new QLabel(T("Видео:"), this));
    r1->addWidget(m_file, 1);
    r1->addWidget(pick);
    root->addLayout(r1);

    // Ссылка на страницу со сценами
    auto *r2 = new QHBoxLayout;
    m_url = new QLineEdit(pageUrl, this);
    m_url->setPlaceholderText(T("Ссылка на фильм на Adult Empire — сцены и тайм-коды заполнятся сами"));
    m_fetch = Ui::primaryButton(T("Загрузить сцены"), this);
    r2->addWidget(new QLabel(T("Страница:"), this));
    r2->addWidget(m_url, 1);
    r2->addWidget(m_fetch);
    root->addLayout(r2);

    // Таблица глав
    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({T("Начало"), T("Конец"), T("Название"), T("Оригинал")});
    m_table->horizontalHeader()->setSectionResizeMode(ColStart, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColEnd, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColTitle, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColOriginal, QHeaderView::Stretch);
    m_table->verticalHeader()->setDefaultSectionSize(30);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(false);
    Ui::themedStyle(m_table, QStringLiteral(
        "QTableWidget{background:#171113;gridline-color:#33282C;border:1px solid #33282C;}"
        "QHeaderView::section{background:#261E21;color:#CFC3A8;border:none;border-bottom:1px solid #33282C;padding:6px;}"
        "QTableWidget::item:selected{background:#4A3828;color:#FFF6DE;}"));
    root->addWidget(m_table, 1);

    // Кнопки правки
    auto *r3 = new QHBoxLayout;
    auto *add = new QPushButton(T("+ Добавить"), this);
    auto *del = new QPushButton(T("− Удалить"), this);
    auto *up = new QPushButton(QStringLiteral("↑"), this);
    auto *down = new QPushButton(QStringLiteral("↓"), this);
    m_translate = new QPushButton(T("Перевести на русский"), this);
    m_caps = new QCheckBox(T("Каждое слово с большой буквы"), this);
    m_caps->setObjectName(QStringLiteral("PlainCheck"));
    m_caps->setChecked(QSettings().value(QStringLiteral("chapters/titleCase"), true).toBool());
    auto *imp = new QPushButton(T("Импорт…"), this);
    auto *exp = new QPushButton(T("Экспорт…"), this);
    for (QPushButton *b : {up, down}) b->setFixedWidth(40);
    r3->addWidget(add);
    r3->addWidget(del);
    r3->addWidget(up);
    r3->addWidget(down);
    r3->addSpacing(16);
    r3->addWidget(m_translate);
    r3->addWidget(m_caps);
    r3->addStretch();
    r3->addWidget(imp);
    r3->addWidget(exp);
    root->addLayout(r3);

    m_state = new QLabel(this);
    m_state->setObjectName(QStringLiteral("StatusLabel"));
    m_state->setWordWrap(true);
    root->addWidget(m_state);

    auto *r4 = new QHBoxLayout;
    m_replace = new QCheckBox(T("Заменить исходный файл (иначе рядом будет создана копия с главами)"), this);
    m_replace->setObjectName(QStringLiteral("PlainCheck"));
    auto *close = new QPushButton(T("Закрыть"), this);
    // Кнопка с выбором действия: основная часть выполняет, стрелка открывает список
    m_save = Ui::primaryButton(QString(), this);
    m_save->setMinimumWidth(250);
    m_saveMenu = Ui::primaryButton(QStringLiteral("▾"), this);
    m_saveMenu->setFixedWidth(34);
    m_saveMenu->setToolTip(T("Выбрать действие"));
    auto *menu = new QMenu(this);
    menu->addAction(T("Переупаковать в MKV и добавить сцены (без перекодирования)"), this, [this] { setMode(Remux); });
    menu->addAction(T("Записать главы в исходный формат"), this, [this] { setMode(WriteInPlace); });
    menu->addAction(T("Конвертировать с главами (формат из раздела «Конвертировать»)"), this, [this] { setMode(Convert); });
    connect(m_saveMenu, &QPushButton::clicked, this, [this, menu] {
        menu->exec(m_saveMenu->mapToGlobal(QPoint(m_saveMenu->width() - menu->sizeHint().width(), m_saveMenu->height())));
    });
    auto *split = new QHBoxLayout;
    split->setSpacing(1);
    split->addWidget(m_save);
    split->addWidget(m_saveMenu);
    r4->addWidget(m_replace);
    r4->addStretch();
    r4->addWidget(close);
    r4->addLayout(split);
    root->addLayout(r4);

    connect(pick, &QPushButton::clicked, this, &ChaptersDialog::chooseFile);
    connect(m_fetch, &QPushButton::clicked, this, &ChaptersDialog::fetchScenes);
    connect(m_translate, &QPushButton::clicked, this, &ChaptersDialog::translateTitles);
    connect(m_caps, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("chapters/titleCase"), on);
        if (on) applyCase();
    });
    connect(add, &QPushButton::clicked, this, [this] {
        Chapter c;
        const int row = m_table->currentRow();
        if (row >= 0) {
            bool ok = false;
            c.start = Chapters::parseTime(m_table->item(row, ColEnd)->text(), &ok);
            if (!ok) c.start = Chapters::parseTime(m_table->item(row, ColStart)->text()) + 60;
        }
        c.title = T("Новая глава");
        addRow(c, row >= 0 ? row + 1 : -1);
    });
    connect(del, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row >= 0) m_table->removeRow(row);
    });
    connect(up, &QPushButton::clicked, this, [this] { moveRow(-1); });
    connect(down, &QPushButton::clicked, this, [this] { moveRow(1); });
    connect(imp, &QPushButton::clicked, this, &ChaptersDialog::importOgm);
    connect(exp, &QPushButton::clicked, this, &ChaptersDialog::exportOgm);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_save, &QPushButton::clicked, this, &ChaptersDialog::save);

    setMode(QSettings().value(QStringLiteral("chapters/saveMode"), int(Remux)).toInt());

#ifndef HAVE_WEBENGINE
    m_fetch->setEnabled(false);
    m_translate->setEnabled(false);
#endif
    if (!file.isEmpty())
        loadFile(file);
    if (!pageUrl.isEmpty() && !file.isEmpty())
        QMetaObject::invokeMethod(this, &ChaptersDialog::fetchScenes, Qt::QueuedConnection);
}

void ChaptersDialog::chooseFile()
{
    const QString f = QFileDialog::getOpenFileName(this, T("Видео для глав"), Settings::lastOpenDir(),
                                                   T("Видео (*.mkv *.mp4 *.m4v *.mov);;Все файлы (*)"));
    if (!f.isEmpty())
        loadFile(f);
}

void ChaptersDialog::loadFile(const QString &file)
{
    m_file->setText(QDir::toNativeSeparators(file));
    Settings::setLastOpenDir(QFileInfo(file).absolutePath());
    const QList<Chapter> existing = Chapters::read(file, &m_duration);
    if (!existing.isEmpty()) {
        setChapters(existing);
        m_state->setText(T("В файле уже есть глав: %1. Их можно изменить или загрузить сцены с сайта.").arg(existing.size()));
    } else {
        m_state->setText(T("Длительность видео: %1. Глав в файле нет.").arg(formatDuration(m_duration)));
    }
}

void ChaptersDialog::setBusy(bool busy, const QString &text)
{
    m_fetch->setEnabled(!busy);
    m_translate->setEnabled(!busy);
    m_save->setEnabled(!busy);
    m_saveMenu->setEnabled(!busy);
    if (!text.isEmpty())
        m_state->setText(text);
}

void ChaptersDialog::fetchScenes()
{
#ifdef HAVE_WEBENGINE
    const QUrl url = QUrl::fromUserInput(m_url->text().trimmed());
    if (!url.isValid() || url.host().isEmpty()) {
        m_state->setText(T("Вставьте ссылку на страницу фильма."));
        return;
    }
    setBusy(true, T("Загрузка сцен…"));
    auto *f = new SceneFetcher(this);
    connect(f, &SceneFetcher::progress, m_state, &QLabel::setText);
    connect(f, &SceneFetcher::finished, this, [this, f](const QList<Chapter> &scenes) {
        f->deleteLater();
        setChapters(scenes);
        setBusy(false, T("Найдено сцен: %1. Перевожу названия на русский…").arg(scenes.size()));
        translateTitles();
    });
    connect(f, &SceneFetcher::failed, this, [this, f](const QString &reason) {
        f->deleteLater();
        setBusy(false, reason);
    });
    f->start(url);
#endif
}

void ChaptersDialog::translateTitles()
{
#ifdef HAVE_WEBENGINE
    QStringList src;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QString o = m_table->item(r, ColOriginal)->text().trimmed();
        if (o.isEmpty())
            o = m_table->item(r, ColTitle)->text().trimmed();
        src << o;
    }
    if (src.isEmpty())
        return;
    setBusy(true, T("Перевод названий на русский…"));
    auto *tr = new Translator(this);
    connect(tr, &Translator::finished, this, [this, tr](const QStringList &lines) {
        tr->deleteLater();
        for (int r = 0; r < m_table->rowCount() && r < lines.size(); ++r) {
            QString t = lines[r];
            if (m_caps->isChecked())
                t = Chapters::titleCase(t);
            m_table->item(r, ColTitle)->setText(t);
        }
        setBusy(false, T("Готово. Проверьте названия и тайм-коды — их можно исправить прямо в таблице."));
    });
    connect(tr, &Translator::failed, this, [this, tr](const QString &reason) {
        tr->deleteLater();
        setBusy(false, reason);
    });
    tr->translate(src);
#endif
}

void ChaptersDialog::applyCase()
{
    for (int r = 0; r < m_table->rowCount(); ++r)
        m_table->item(r, ColTitle)->setText(Chapters::titleCase(m_table->item(r, ColTitle)->text()));
}

void ChaptersDialog::addRow(const Chapter &c, int at)
{
    const int row = at < 0 ? m_table->rowCount() : at;
    m_table->insertRow(row);
    m_table->setItem(row, ColStart, new QTableWidgetItem(Chapters::formatTime(c.start)));
    m_table->setItem(row, ColEnd, new QTableWidgetItem(c.end > 0 ? Chapters::formatTime(c.end) : QString()));
    m_table->setItem(row, ColTitle, new QTableWidgetItem(c.title));
    auto *orig = new QTableWidgetItem(c.original);
    orig->setForeground(Theme::color("#A99F90"));
    m_table->setItem(row, ColOriginal, orig);
}

void ChaptersDialog::setChapters(const QList<Chapter> &list)
{
    m_table->setRowCount(0);
    for (const Chapter &c : list)
        addRow(c);
}

QList<Chapter> ChaptersDialog::chapters(bool *okAll) const
{
    QList<Chapter> out;
    bool good = true;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        Chapter c;
        bool ok = false;
        c.start = Chapters::parseTime(m_table->item(r, ColStart)->text(), &ok);
        if (!ok) good = false;
        const QString e = m_table->item(r, ColEnd)->text().trimmed();
        if (!e.isEmpty()) c.end = Chapters::parseTime(e);
        c.title = m_table->item(r, ColTitle)->text().trimmed();
        c.original = m_table->item(r, ColOriginal)->text();
        out << c;
    }
    std::sort(out.begin(), out.end(), [](const Chapter &a, const Chapter &b) { return a.start < b.start; });
    if (okAll) *okAll = good;
    return out;
}

void ChaptersDialog::moveRow(int delta)
{
    const int row = m_table->currentRow();
    const int to = row + delta;
    if (row < 0 || to < 0 || to >= m_table->rowCount())
        return;
    for (int c = 0; c < m_table->columnCount(); ++c) {
        QTableWidgetItem *a = m_table->takeItem(row, c);
        QTableWidgetItem *b = m_table->takeItem(to, c);
        m_table->setItem(row, c, b);
        m_table->setItem(to, c, a);
    }
    m_table->setCurrentCell(to, m_table->currentColumn());
}

void ChaptersDialog::setMode(int mode)
{
    m_mode = qBound(0, mode, 2);
    QSettings().setValue(QStringLiteral("chapters/saveMode"), m_mode);
    m_save->setText(m_mode == Remux ? T("Переупаковать в MKV") : m_mode == WriteInPlace ? T("Записать главы") : T("Конвертировать"));
    m_replace->setEnabled(m_mode != Convert);
}

void ChaptersDialog::save()
{
    const QString file = QDir::fromNativeSeparators(m_file->text());
    if (file.isEmpty() || !QFile::exists(file)) {
        m_state->setText(T("Сначала выберите видеофайл."));
        return;
    }
    bool ok = false;
    const QList<Chapter> list = chapters(&ok);
    if (!ok) {
        m_state->setText(T("Проверьте время начала глав: формат чч:мм:сс."));
        return;
    }
    const QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/aristocrat-vc");
    QDir().mkpath(tmpDir);
    const QString meta = tmpDir + QStringLiteral("/chapters-%1.txt").arg(QDateTime::currentMSecsSinceEpoch());
    QFile mf(meta);
    if (!mf.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    mf.write(Chapters::metadataFile(list, m_duration).toUtf8());
    mf.close();

    const QFileInfo fi(file);
    if (m_mode == Convert) {
        emit convertRequested(file, fi.completeBaseName(), fi.absolutePath(), meta);
        m_state->setText(T("Видео добавлено в раздел «Конвертировать» — главы вставятся после конвертации."));
        return;
    }

    // Куда писать: при замене — во временный файл, потом на место исходного
    const bool remux = m_mode == Remux;
    const QString ext = remux ? QStringLiteral("mkv") : fi.suffix();
    QString finalPath = fi.dir().filePath(fi.completeBaseName() + QLatin1Char('.') + ext);
    if (!m_replace->isChecked() && QFileInfo(finalPath) == fi)
        finalPath = fi.dir().filePath(fi.completeBaseName() + T(" (с главами).") + ext);
    const QString out = fi.dir().filePath(fi.completeBaseName() + QStringLiteral(".chapters-tmp.") + ext);
    const bool replace = m_replace->isChecked();

    setBusy(true, remux ? T("Переупаковка в MKV…") : T("Запись глав в видео…"));
    m_proc = new QProcess(this);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, file, out, finalPath, replace, meta](int code) {
        const QString err = QString::fromUtf8(m_proc->readAllStandardError()).trimmed();
        m_proc->deleteLater();
        m_proc = nullptr;
        QFile::remove(meta);
        if (code != 0) {
            QFile::remove(out);
            setBusy(false, T("Не удалось записать главы: ") + err.section('\n', -1));
            return;
        }
        QString result = out;
        if (QFileInfo(finalPath) == QFileInfo(file)) {
            // Тот же файл: заменить исходный
            if (QFile::remove(file) && QFile::rename(out, file))
                result = file;
        } else {
            QFile::remove(finalPath);
            if (QFile::rename(out, finalPath))
                result = finalPath;
            if (replace)
                QFile::remove(file);   // исходный MP4/AVI больше не нужен — остаётся MKV
        }
        setBusy(false, T("Главы записаны: %1").arg(QDir::toNativeSeparators(result)));
        m_file->setText(QDir::toNativeSeparators(result));
        Ui::showInFolder(result);
    });
    m_proc->start(Tools::ffmpeg(), remux ? Chapters::remuxArgs(file, meta, out) : Chapters::muxArgs(file, meta, out));
}

void ChaptersDialog::importOgm()
{
    const QString f = QFileDialog::getOpenFileName(this, T("Импорт глав"), Settings::lastOpenDir(), T("Главы (*.txt);;Все файлы (*)"));
    if (f.isEmpty())
        return;
    QFile file(f);
    if (file.open(QIODevice::ReadOnly))
        setChapters(Chapters::fromOgm(QString::fromUtf8(file.readAll())));
}

void ChaptersDialog::exportOgm()
{
    const QString f = QFileDialog::getSaveFileName(this, T("Экспорт глав"), Settings::lastOpenDir() + QStringLiteral("/chapters.txt"),
                                                   T("Главы (*.txt)"));
    if (f.isEmpty())
        return;
    QFile file(f);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(Chapters::toOgm(chapters()).toUtf8());
}
