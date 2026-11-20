#include "ConvertPage.h"
#include "core/I18n.h"
#include "DropZone.h"
#include "FormatPopup.h"
#include "Frameless.h"
#include "TaskCard.h"
#include "Widgets.h"
#include "core/FfmpegCommand.h"
#include "core/Settings.h"
#include "core/Tools.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

static const QStringList kMediaExt = {
    "mp4", "m4v", "mkv", "avi", "mov", "wmv", "flv", "webm", "mpg", "mpeg", "m2v", "vob", "ts", "mts", "m2ts",
    "3gp", "3g2", "ogv", "dv", "asf", "rm", "rmvb", "divx", "xvid", "f4v", "mxf",
    "mp3", "wav", "flac", "aac", "m4a", "ogg", "oga", "opus", "wma", "ac3", "amr", "aiff", "ape"};

static QString mediaFilter()
{
    QStringList pats;
    for (const QString &e : kMediaExt)
        pats << QStringLiteral("*.") + e;
    return T("Видео и аудио (%1);;Все файлы (*)").arg(pats.join(' '));
}

ConvertPage::ConvertPage(QWidget *parent)
    : QWidget(parent), m_engine(new ConversionEngine(this))
{
    setAcceptDrops(true);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(buildSubBar());

    m_stack = new QStackedWidget(this);
    // 0: пусто (зона перетаскивания)
    auto *empty = new QWidget(this);
    auto *el = new QVBoxLayout(empty);
    auto *dz = new DropZone(empty);
    el->addStretch();
    el->addWidget(dz, 0, Qt::AlignCenter);
    el->addStretch();
    connect(dz, &DropZone::clicked, this, &ConvertPage::chooseFiles);
    m_stack->addWidget(empty);

    // 1: список задач
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("TaskArea"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *list = new QWidget(scroll);
    m_cardLayout = new QVBoxLayout(list);
    m_cardLayout->setContentsMargins(0, 0, 0, 0);
    m_cardLayout->setSpacing(0);
    m_cardLayout->addStretch();
    scroll->setWidget(list);
    m_stack->addWidget(scroll);

    // 2: готовые
    m_stack->addWidget(buildConvertedPage());
    root->addWidget(m_stack, 1);
    root->addWidget(buildBottomBar());

    connect(m_engine, &ConversionEngine::taskChanged, this, &ConvertPage::onTaskChanged);
    connect(m_engine, &ConversionEngine::queueFinished, this, &ConvertPage::onQueueFinished);
    connect(m_engine, &ConversionEngine::message, this, [this](const QString &m) { m_statusLine->setText(m); });
    connect(m_engine, &ConversionEngine::taskFinished, this, [this](int id) {
        for (const TaskPtr &t : m_tasks)
            if (t->id == id && t->status == ConversionTask::Done)
                addConvertedEntry(t);
    });

    if (!Tools::hasFfmpeg())
        m_statusLine->setText(T("⚠ FFmpeg не найден — положите ffmpeg и ffprobe в папку tools рядом с программой"));
    updateView();
}

// ---------------- Верхняя панель ----------------

QWidget *ConvertPage::buildSubBar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("SubBar"));
    bar->setAttribute(Qt::WA_StyledBackground);
    bar->setFixedHeight(52);
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(30, 0, 30, 0);

    auto *add = new QToolButton(bar);
    add->setObjectName(QStringLiteral("FlatTool"));
    add->setText(T("Добавить файлы"));
    add->setIcon(Ui::icon("add", true));
    add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    add->setPopupMode(QToolButton::MenuButtonPopup);
    add->setCursor(Qt::PointingHandCursor);
    auto *addMenu = new QMenu(add);
    addMenu->addAction(T("Добавить файлы…"), this, &ConvertPage::chooseFiles);
    addMenu->addAction(T("Добавить папку…"), this, &ConvertPage::chooseFolder);
    add->setMenu(addMenu);
    connect(add, &QToolButton::clicked, this, &ConvertPage::chooseFiles);

    auto *dvd = new QToolButton(bar);
    dvd->setObjectName(QStringLiteral("FlatTool"));
    dvd->setText(T("Загрузить DVD"));
    dvd->setIcon(Ui::icon("dvd", true));
    dvd->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    dvd->setPopupMode(QToolButton::MenuButtonPopup);
    dvd->setCursor(Qt::PointingHandCursor);
    auto *dvdMenu = new QMenu(dvd);
    dvdMenu->addAction(T("Папка DVD (VIDEO_TS)…"), this, &ConvertPage::loadDvdFolder);
    dvdMenu->addAction(T("Образ ISO…"), this, [this] {
        Ui::message(this, T("Образ ISO"),
                    T("Откройте образ как диск (в Windows 8+ — двойной щелчок, в Linux — «Открыть с помощью» → "
                                                "подключение образа), затем выберите «Папка DVD (VIDEO_TS)» и укажите папку VIDEO_TS на нём."));
    });
    dvd->setMenu(dvdMenu);
    connect(dvd, &QToolButton::clicked, this, &ConvertPage::loadDvdFolder);

    m_segConverting = new QPushButton(T("Конвертируется"), bar);
    m_segConverting->setObjectName(QStringLiteral("SegLeft"));
    m_segConverted = new QPushButton(T("Готово"), bar);
    m_segConverted->setObjectName(QStringLiteral("SegRight"));
    for (QPushButton *b : {m_segConverting, m_segConverted}) {
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setCursor(Qt::PointingHandCursor);
    }
    m_segConverting->setChecked(true);
    connect(m_segConverting, &QPushButton::clicked, this, &ConvertPage::updateView);
    connect(m_segConverted, &QPushButton::clicked, this, &ConvertPage::updateView);

    auto *lbl = new QLabel(T("Конвертировать все в:"), bar);
    m_globalFormat = new QPushButton(bar);
    m_globalFormat->setMinimumWidth(200);
    m_globalFormat->setIcon(Ui::icon("chevron", true));
    m_globalFormat->setLayoutDirection(Qt::RightToLeft);
    m_globalFormat->setStyleSheet(QStringLiteral("QPushButton{background:#171113;text-align:left;padding:6px 10px;}"));
    m_globalFormat->setCursor(Qt::PointingHandCursor);
    connect(m_globalFormat, &QPushButton::clicked, this, [this] {
        auto *popup = new FormatPopup(window());
        popup->setCurrent(Settings::globalPresetId());
        connect(popup, &FormatPopup::presetChosen, this, &ConvertPage::applyGlobalPreset);
        popup->showBelow(m_globalFormat);
    });
    updateGlobalButton();

    lay->addWidget(add);
    lay->addSpacing(20);
    lay->addWidget(dvd);
    lay->addStretch();
    lay->addWidget(m_segConverting);
    lay->addWidget(m_segConverted);
    lay->addStretch();
    lay->addWidget(lbl);
    lay->addSpacing(8);
    lay->addWidget(m_globalFormat);
    return bar;
}

// ---------------- Нижняя панель ----------------

QWidget *ConvertPage::buildBottomBar()
{
    auto *outer = new QWidget(this);
    auto *ol = new QVBoxLayout(outer);
    ol->setContentsMargins(0, 0, 0, 0);
    ol->setSpacing(0);

    m_statusLine = new QLabel(outer);
    m_statusLine->setObjectName(QStringLiteral("StatusLabel"));
    m_statusLine->setContentsMargins(30, 4, 30, 4);
    m_statusLine->setStyleSheet(QStringLiteral("background:#1F191B;"));
    ol->addWidget(m_statusLine);

    auto *bar = new QWidget(outer);
    bar->setObjectName(QStringLiteral("BottomBar"));
    bar->setAttribute(Qt::WA_StyledBackground);
    bar->setFixedHeight(72);
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(30, 0, 40, 0);
    lay->setSpacing(12);

    m_alarm = Ui::iconButton("alarm", T("Действие после завершения"), bar, 24);
    m_alarm->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(m_alarm);
    auto *grp = new QActionGroup(menu);
    const QStringList names = {T("Ничего не делать"), T("Закрыть программу"),
                               T("Спящий режим"), T("Выключить компьютер")};
    for (int i = 0; i < names.size(); ++i) {
        QAction *a = menu->addAction(names[i]);
        a->setCheckable(true);
        a->setChecked(Settings::afterDone() == i);
        grp->addAction(a);
        connect(a, &QAction::triggered, this, [this, i] {
            Settings::setAfterDone(i);
            m_alarm->setIcon(Ui::icon("alarm", i != 0));
        });
    }
    m_alarm->setMenu(menu);
    m_alarm->setIcon(Ui::icon("alarm", Settings::afterDone() != 0));
    m_alarm->setStyleSheet(QStringLiteral("QToolButton::menu-indicator{image:none;}"));

    m_hw = Ui::iconButton("bolt", QString(), bar, 24);
    m_hw->setCheckable(true);
    m_hw->setChecked(Settings::hardwareAccel());
    auto updHw = [this] {
        m_hw->setIcon(Ui::icon("bolt", m_hw->isChecked()));
        m_hw->setToolTip(m_hw->isChecked() ? T("Ускорение видеокартой: ВКЛ (NVIDIA / Intel / AMD, если доступно)")
                                           : T("Ускорение видеокартой: ВЫКЛ"));
    };
    updHw();
    connect(m_hw, &QToolButton::toggled, this, [this, updHw](bool on) { Settings::setHardwareAccel(on); updHw(); });

    auto *sep1 = new QFrame(bar);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setStyleSheet(QStringLiteral("color:#33282C;"));
    auto *sep2 = new QFrame(bar);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setStyleSheet(QStringLiteral("color:#33282C;"));

    auto *outLbl = new QLabel(T("Папка:"), bar);
    m_outDir = new QComboBox(bar);
    m_outDir->setEditable(true);
    m_outDir->setMinimumWidth(420);
    m_outDir->addItem(Settings::outputDir());
    for (const QString &d : Settings::recentOutputDirs())
        if (d != Settings::outputDir())
            m_outDir->addItem(d);
    m_outDir->addItem(T("Выбрать папку…"));
    m_outDir->setCurrentIndex(0);
    connect(m_outDir, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        if (i == m_outDir->count() - 1)
            chooseOutputDir();
        else
            Settings::setOutputDir(m_outDir->itemText(i));
    });
    connect(m_outDir->lineEdit(), &QLineEdit::editingFinished, this, [this] {
        const QString d = m_outDir->currentText().trimmed();
        if (!d.isEmpty() && d != T("Выбрать папку…"))
            Settings::setOutputDir(d);
    });
    auto *openDir = Ui::iconButton("folder", T("Открыть папку с результатами"), bar, 22);
    openDir->setIcon(Ui::icon("folder", true));
    connect(openDir, &QToolButton::clicked, this, [this] {
        QDir().mkpath(outputDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(outputDir()));
    });

    auto *mergeLbl = new QLabel(T("Объединить все видео"), bar);
    m_merge = new QCheckBox(bar);
    m_merge->setChecked(Settings::mergeAll());
    m_merge->setToolTip(T("После конвертации склеить все файлы в один (формат у всех должен быть одинаковым)"));
    connect(m_merge, &QCheckBox::toggled, this, [](bool on) { Settings::setMergeAll(on); });

    m_convertAll = Ui::primaryButton(T("Конвертировать все"), bar);
    m_convertAll->setMinimumWidth(180);
    m_convertAll->setMinimumHeight(48);
    connect(m_convertAll, &QPushButton::clicked, this, [this] {
        if (m_engine->isRunning())
            m_engine->stop();
        else
            convertAll();
    });

    lay->addWidget(m_alarm);
    lay->addWidget(sep1);
    lay->addWidget(m_hw);
    lay->addWidget(sep2);
    lay->addSpacing(8);
    lay->addWidget(outLbl);
    lay->addWidget(m_outDir);
    lay->addWidget(openDir);
    lay->addStretch();
    lay->addWidget(mergeLbl);
    lay->addWidget(m_merge);
    lay->addSpacing(20);
    lay->addWidget(m_convertAll);
    ol->addWidget(bar);
    return outer;
}

QWidget *ConvertPage::buildConvertedPage()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    m_converted = new QListWidget(w);
    m_converted->setSelectionMode(QAbstractItemView::NoSelection);
    lay->addWidget(m_converted);
    return w;
}

void ConvertPage::addConvertedEntry(const TaskPtr &t)
{
    auto *it = new QListWidgetItem;
    it->setSizeHint(QSize(100, 86));
    m_converted->insertItem(0, it);
    auto *row = new QWidget;
    row->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(36, 8, 30, 8);
    h->setSpacing(18);
    auto *thumb = new QLabel(row);
    thumb->setFixedSize(112, 63);
    thumb->setObjectName(QStringLiteral("Thumb"));
    if (!t->thumbnail.isNull())
        thumb->setPixmap(QPixmap::fromImage(t->thumbnail.scaled(thumb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    else
        thumb->setPixmap(Ui::formatBadge(t->preset.ext.toUpper(), 56));
    thumb->setAlignment(Qt::AlignCenter);
    const QFileInfo fi(t->outputPath);
    auto *text = new QLabel(QStringLiteral("<span style='color:#E9E1D2;font-size:11pt'>%1</span><br>"
                                           "<span style='color:#A99F90'>%2 · %3 · %4</span>")
                                .arg(fi.fileName().toHtmlEscaped(), t->preset.ext.toUpper(), formatSize(fi.size()),
                                     formatDuration(t->effectiveDuration())),
                            row);
    auto *play = Ui::iconButton("play", T("Воспроизвести"), row, 24);
    auto *folder = Ui::iconButton("folder", T("Показать в папке"), row, 22);
    h->addWidget(thumb);
    h->addWidget(text, 1);
    h->addWidget(play);
    h->addWidget(folder);
    m_converted->setItemWidget(it, row);
    const QString path = t->outputPath;
    connect(play, &QToolButton::clicked, this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
    connect(folder, &QToolButton::clicked, this, [path] {
#ifdef Q_OS_WIN
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
    });
    m_segConverted->setText(T("Готово (%1)").arg(m_converted->count()));
}

// ---------------- Добавление файлов ----------------

void ConvertPage::chooseFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, T("Добавить файлы"), Settings::lastOpenDir(), mediaFilter());
    if (files.isEmpty())
        return;
    Settings::setLastOpenDir(QFileInfo(files.first()).absolutePath());
    addFiles(files);
}

void ConvertPage::chooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, T("Добавить папку"), Settings::lastOpenDir());
    if (dir.isEmpty())
        return;
    Settings::setLastOpenDir(dir);
    addFiles({dir});
}

void ConvertPage::loadDvdFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, T("Папка DVD (VIDEO_TS)"), Settings::lastOpenDir());
    if (dir.isEmpty())
        return;
    if (QDir(dir + "/VIDEO_TS").exists())
        dir += "/VIDEO_TS";
    // Находим самый большой фильм (VTS_xx) и склеиваем его части VOB через concat:
    QDir d(dir);
    QHash<QString, qint64> sizes;
    QHash<QString, QStringList> parts;
    for (const QFileInfo &fi : d.entryInfoList({"VTS_*_*.VOB", "vts_*_*.vob"}, QDir::Files, QDir::Name)) {
        const QStringList p = fi.completeBaseName().split('_');
        if (p.size() < 3 || p[2] == QLatin1String("0"))
            continue;   // VTS_xx_0 — меню
        sizes[p[1]] += fi.size();
        parts[p[1]] << fi.absoluteFilePath();
    }
    if (sizes.isEmpty()) {
        Ui::message(this, QStringLiteral("DVD"), T("В папке нет файлов VTS_xx_x.VOB."));
        return;
    }
    QString name = QDir(dir).dirName();
    if (name.compare("VIDEO_TS", Qt::CaseInsensitive) == 0) {
        QDir up(dir);
        up.cdUp();
        name = up.dirName();
    }
    if (name.isEmpty())
        name = QStringLiteral("DVD");
    QStringList titles;
    if (QSettings().value("convert/dvdMode", 0).toInt() == 1) {
        // Все фильмы на диске (пропускаем совсем короткие — заставки)
        for (auto it = sizes.begin(); it != sizes.end(); ++it)
            if (it.value() > 30 * 1024 * 1024)
                titles << it.key();
        titles.sort();
    }
    if (titles.isEmpty()) {
        QString best;
        for (auto it = sizes.begin(); it != sizes.end(); ++it)
            if (best.isEmpty() || it.value() > sizes[best])
                best = it.key();
        titles << best;
    }
    for (const QString &title : titles) {
        QStringList files = parts[title];
        files.sort();
        addTask(QStringLiteral("concat:") + files.join('|'), titles.size() > 1 ? name + QStringLiteral(" — ") + title : name);
    }
    updateView();
}

void ConvertPage::addFiles(const QStringList &paths)
{
    for (const QString &p : paths) {
        QFileInfo fi(p);
        if (fi.isDir()) {
            QDirIterator it(p, QDir::Files, QDirIterator::Subdirectories);
            QStringList found;
            while (it.hasNext()) {
                const QString f = it.next();
                if (kMediaExt.contains(QFileInfo(f).suffix().toLower()))
                    found << f;
            }
            found.sort();
            for (const QString &f : found)
                addTask(f);
        } else if (fi.exists()) {
            addTask(p);
        }
    }
    m_segConverting->setChecked(true);
    updateView();
}

void ConvertPage::addFilesWithUpscale(const QStringList &paths, UpscaleOptions::Mode mode, bool vhs)
{
    const int before = m_tasks.size();
    addFiles(paths);
    for (int i = before; i < m_tasks.size(); ++i) {
        m_tasks[i]->upscale.mode = mode;
        m_tasks[i]->upscale.vhsRestore = vhs;
        m_tasks[i]->preset = PresetStore::instance().find(QStringLiteral("mp4-source"));
        if (TaskCard *c = m_cards.value(m_tasks[i]->id))
            c->refresh();
    }
}

TaskPtr ConvertPage::addTask(const QString &input, const QString &displayName)
{
    TaskPtr t = TaskPtr::create();
    t->id = m_nextId++;
    t->input = input;
    t->preset = PresetStore::instance().find(Settings::globalPresetId());
    if (!t->preset.isValid())
        t->preset = PresetStore::instance().defaultPreset();
    t->outputName = displayName.isEmpty() ? QFileInfo(input).completeBaseName() : displayName;
    m_tasks << t;

    auto *card = new TaskCard(t, this);
    m_cards.insert(t->id, card);
    m_cardLayout->insertWidget(m_cardLayout->count() - 1, card);

    connect(card, &TaskCard::convertRequested, this, &ConvertPage::convertOne);
    connect(card, &TaskCard::stopRequested, m_engine, &ConversionEngine::stop);
    connect(card, &TaskCard::removeRequested, this, &ConvertPage::removeTask);
    connect(card, &TaskCard::applyEditToAll, this, [this](const EditOptions &e) {
        for (const TaskPtr &x : m_tasks) {
            if (x->status == ConversionTask::Running || x->status == ConversionTask::Waiting)
                continue;
            const double ts = x->edit.trimStart, te = x->edit.trimEnd;
            const int at = x->edit.audioTrack;
            x->edit = e;
            x->edit.trimStart = ts;   // обрезка по времени у каждого своя
            x->edit.trimEnd = te;
            x->edit.audioTrack = qMin(at, x->info.audioTracks.size() - 1);
            if (e.autoBars && x->probed) {
                // Чёрные поля у каждого файла свои — ищем заново
                x->edit.crop = QRect();
                auto rect = QSharedPointer<QRect>::create();
                ConversionTask copy = *x;
                Ui::runAsync(this, [copy, rect] { *rect = FfmpegCommand::detectBlackBars(copy); }, [this, x, rect] {
                    x->edit.crop = *rect;
                    if (TaskCard *c = m_cards.value(x->id))
                        c->refresh();
                });
            }
            if (TaskCard *c = m_cards.value(x->id))
                c->refresh();
        }
    });

    Ui::runAsync(card, [t] { t->info = MediaInfo::probe(t->input); }, [this, t, card] {
        t->probed = true;
        if (t->info.audioTracks.isEmpty())
            t->edit.audioTrack = -1;
        // Чересстрочное видео (кассеты, DVD, ТВ) — сразу включаем устранение гребёнки
        if (t->info.interlaced)
            t->edit.deinterlace = true;
        card->setInfoLoaded();
        if (m_autoStart.remove(t->id) && t->info.ok)
            convertOne(t);
    });
    return t;
}

void ConvertPage::addFileAndConvert(const QString &file, bool autoStart)
{
    TaskPtr t = addTask(file);
    if (autoStart)
        m_autoStart.insert(t->id);
    m_segConverting->setChecked(true);
    updateView();
}

void ConvertPage::removeTask(const TaskPtr &t)
{
    if (t->status == ConversionTask::Running) {
        Ui::message(this, T("Идёт конвертация"), T("Сначала остановите конвертацию этого файла."));
        return;
    }
    if (t->status == ConversionTask::Waiting)
        return;
    m_tasks.removeAll(t);
    if (TaskCard *c = m_cards.take(t->id))
        c->deleteLater();
    updateView();
}

// ---------------- Конвертация ----------------

QString ConvertPage::outputDir() const
{
    QString d = m_outDir->currentText().trimmed();
    if (d.isEmpty() || d == T("Выбрать папку…"))
        d = Settings::outputDir();
    return d;
}

void ConvertPage::chooseOutputDir()
{
    const QString d = QFileDialog::getExistingDirectory(this, T("Папка для результатов"), Settings::outputDir());
    if (!d.isEmpty()) {
        Settings::setOutputDir(d);
        m_outDir->insertItem(0, d);
        m_outDir->setCurrentIndex(0);
    } else {
        m_outDir->setCurrentIndex(0);
    }
}

void ConvertPage::convertAll()
{
    QList<TaskPtr> todo;
    for (const TaskPtr &t : m_tasks)
        if (t->probed && t->info.ok && t->status != ConversionTask::Done)
            todo << t;
    if (todo.isEmpty()) {
        m_statusLine->setText(m_tasks.isEmpty() ? T("Добавьте файлы для конвертации")
                                                : T("Нет файлов, ожидающих конвертации"));
        return;
    }
    Settings::setOutputDir(outputDir());
    m_statusLine->clear();
    m_engine->start(todo, outputDir(), m_hw->isChecked(), m_merge->isChecked());
    m_convertAll->setText(T("Остановить"));
}

void ConvertPage::convertOne(const TaskPtr &t)
{
    if (m_engine->isRunning()) {
        m_engine->enqueue(t);
        return;
    }
    Settings::setOutputDir(outputDir());
    m_statusLine->clear();
    t->status = ConversionTask::Ready;
    m_engine->start({t}, outputDir(), m_hw->isChecked(), false);
    m_convertAll->setText(T("Остановить"));
}

void ConvertPage::onTaskChanged(int id)
{
    if (TaskCard *c = m_cards.value(id))
        c->refresh();
}

void ConvertPage::onQueueFinished(const QStringList &outputs, int failed)
{
    m_convertAll->setText(T("Конвертировать все"));
    if (failed > 0)
        m_statusLine->setText(T("Готово: %1, с ошибками: %2 (наведите на кнопку «Повторить», чтобы увидеть причину)")
                                  .arg(outputs.size()).arg(failed));
    else if (!outputs.isEmpty())
        m_statusLine->setText(T("Готово файлов: %1").arg(outputs.size()));
    QApplication::alert(window());
    if (QSettings().value("general/sound", true).toBool())
        QApplication::beep();

    const int action = Settings::afterDone();
    if (outputs.isEmpty() || action == Settings::DoNothing)
        return;
    if (action == Settings::CloseApp) {
        qApp->quit();
    } else if (action == Settings::Shutdown) {
#ifdef Q_OS_WIN
        QProcess::startDetached(QStringLiteral("shutdown"), {"/s", "/t", "60"});
#else
        QProcess::startDetached(QStringLiteral("systemctl"), {"poweroff"});
#endif
    } else if (action == Settings::Sleep) {
#ifdef Q_OS_WIN
        QProcess::startDetached(QStringLiteral("rundll32.exe"), {"powrprof.dll,SetSuspendState", "0,1,0"});
#else
        QProcess::startDetached(QStringLiteral("systemctl"), {"suspend"});
#endif
    }
}

void ConvertPage::applyGlobalPreset(const Preset &p)
{
    Settings::setGlobalPresetId(p.id);
    for (const TaskPtr &t : m_tasks) {
        if (t->status == ConversionTask::Running || t->status == ConversionTask::Waiting)
            continue;
        t->preset = p;
        t->customVBitrate = 0;
        t->customResolution = QSize();
        if (t->status == ConversionTask::Done)
            t->status = ConversionTask::Ready;
        if (TaskCard *c = m_cards.value(t->id))
            c->refresh();
    }
    updateGlobalButton();
}

void ConvertPage::updateGlobalButton()
{
    Preset p = PresetStore::instance().find(Settings::globalPresetId());
    if (!p.isValid())
        p = PresetStore::instance().defaultPreset();
    m_globalFormat->setText(p.title());
}

void ConvertPage::updateView()
{
    if (m_segConverted->isChecked())
        m_stack->setCurrentIndex(2);
    else
        m_stack->setCurrentIndex(m_tasks.isEmpty() ? 0 : 1);
    m_convertAll->setEnabled(!m_tasks.isEmpty() || m_engine->isRunning());
}

void ConvertPage::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void ConvertPage::dropEvent(QDropEvent *e)
{
    QStringList paths;
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile())
            paths << u.toLocalFile();
    if (!paths.isEmpty()) {
        addFiles(paths);
        e->acceptProposedAction();
    }
}

void ConvertPage::openFirstTrim()
{
    if (!m_tasks.isEmpty())
        if (TaskCard *c = m_cards.value(m_tasks.first()->id))
            c->openTrim();
}

void ConvertPage::reloadSettings()
{
    m_outDir->setItemText(0, Settings::outputDir());
    m_outDir->setCurrentIndex(0);
    m_hw->setChecked(Settings::hardwareAccel());
}
