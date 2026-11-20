#include "DownloadPage.h"
#include "Widgets.h"
#include "core/Settings.h"
#include "core/Task.h"
#include "core/Tools.h"

#include <QApplication>
#include <QFile>
#include <algorithm>
#include <functional>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSet>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

static QString speedText(double bps)
{
    if (bps <= 0)
        return {};
    if (bps > 1024 * 1024)
        return QString::number(bps / 1024 / 1024, 'f', 1).replace('.', ',') + QStringLiteral(" МБ/с");
    return QString::number(bps / 1024, 'f', 0) + QStringLiteral(" КБ/с");
}

static QString downloadDir()
{
    QSettings s;
    QString def = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (def.isEmpty())
        def = QDir::homePath();
    return s.value("download/dir", def + QStringLiteral("/Aristocrat Downloads")).toString();
}

static QImage grabImageUrl(const QString &url)
{
    // Миниатюру скачивает и перекодирует FFmpeg — ему не нужны отдельные библиотеки SSL.
    if (url.isEmpty() || Tools::ffmpeg().isEmpty())
        return {};
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/aristocrat-vc");
    QDir().mkpath(dir);
    const QString out = dir + QStringLiteral("/thumb_%1.jpg").arg(QUuid::createUuid().toString(QUuid::Id128));
    QProcess p;
    p.start(Tools::ffmpeg(), {"-hide_banner", "-nostdin", "-loglevel", "error", "-y", "-i", url,
                              "-frames:v", "1", "-vf", "scale=320:-2", out});
    if (!p.waitForFinished(20000))
        p.kill();
    QImage img(out);
    QFile::remove(out);
    return img;
}

// ======================= AnalyzeDialog =======================

AnalyzeDialog::AnalyzeDialog(const QString &url, const QStringList &baseArgs, QWidget *parent)
    : FramedDialog(parent), m_item(DownloadPtr::create())
{
    setWindowTitle(QStringLiteral("Загрузка видео"));
    setMinimumWidth(620);
    m_item->url = url;

    auto *root = contentLayout();
    root->setContentsMargins(24, 18, 24, 18);
    root->setSpacing(14);

    auto *top = new QHBoxLayout;
    m_thumb = new QLabel(this);
    m_thumb->setObjectName(QStringLiteral("Thumb"));
    m_thumb->setFixedSize(192, 108);
    m_thumb->setAlignment(Qt::AlignCenter);
    m_thumb->setPixmap(Ui::icon("globe", true).pixmap(40, 40));
    auto *txt = new QVBoxLayout;
    m_title = new QLabel(url, this);
    m_title->setWordWrap(true);
    m_title->setStyleSheet(QStringLiteral("font-size:11pt;"));
    m_state = new QLabel(QStringLiteral("Анализ ссылки…"), this);
    m_state->setObjectName(QStringLiteral("StatusLabel"));
    m_state->setWordWrap(true);
    txt->addWidget(m_title);
    txt->addWidget(m_state);
    txt->addStretch();
    top->addWidget(m_thumb);
    top->addSpacing(12);
    top->addLayout(txt, 1);
    root->addLayout(top);

    auto *qLbl = new QLabel(QStringLiteral("Качество:"), this);
    qLbl->setObjectName(QStringLiteral("Muted"));
    root->addWidget(qLbl);
    m_options = new QVBoxLayout;
    m_group = new QButtonGroup(this);
    root->addLayout(m_options);

    auto *btns = new QHBoxLayout;
    btns->addStretch();
    auto *cancel = new QPushButton(QStringLiteral("Отмена"), this);
    m_ok = Ui::primaryButton(QStringLiteral("Скачать"), this);
    m_ok->setEnabled(false);
    m_ok->setMinimumWidth(140);
    btns->addWidget(cancel);
    btns->addWidget(m_ok);
    root->addLayout(btns);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_ok, &QPushButton::clicked, this, [this] {
        const int id = m_group->checkedId();
        m_item->audioOnly = id == -100;
        m_item->height = id > 0 ? id : 0;
        accept();
    });

    m_proc = new QProcess(this);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code) { onAnalyzed(code); });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            m_state->setText(QStringLiteral("Не удалось запустить yt-dlp."));
        }
    });
    QStringList args = baseArgs;
    args << "-J" << "--no-warnings" << url;
    m_proc->start(Tools::ytdlp(), args);
}

AnalyzeDialog::~AnalyzeDialog()
{
    if (m_proc->state() != QProcess::NotRunning) {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
}

void AnalyzeDialog::onAnalyzed(int code)
{
    const QByteArray out = m_proc->readAllStandardOutput();
    const QJsonDocument doc = QJsonDocument::fromJson(out);
    if (code != 0 || !doc.isObject()) {
        QString err = QString::fromUtf8(m_proc->readAllStandardError()).trimmed();
        const QStringList lines = err.split('\n', Qt::SkipEmptyParts);
        err = lines.isEmpty() ? QStringLiteral("Видео по этой ссылке не найдено.") : lines.last();
        err.remove(QStringLiteral("ERROR: "));
        m_state->setText(QStringLiteral("Не получилось: %1<br><br><span style='color:#A99F90'>Если видео доступно только после входа на сайт, "
                                        "выберите внизу раздела «Вход на сайты» браузер, в котором вы вошли.</span>").arg(err.toHtmlEscaped()));
        m_state->setTextFormat(Qt::RichText);
        return;
    }
    const QJsonObject o = doc.object();
    m_item->title = o.value("title").toString();
    m_item->site = o.value("extractor_key").toString();
    m_item->duration = o.value("duration").toDouble();
    m_item->thumbUrl = o.value("thumbnail").toString();
    m_title->setText(m_item->title);
    m_state->setText(QStringLiteral("%1%2").arg(m_item->site,
                                               m_item->duration > 0 ? QStringLiteral(" · ") + formatDuration(m_item->duration) : QString()));

    // Доступные высоты кадра
    QSet<int> heights;
    for (const QJsonValue &v : o.value("formats").toArray()) {
        const QJsonObject f = v.toObject();
        const int h = f.value("height").toInt();
        if (h > 0 && f.value("vcodec").toString() != QLatin1String("none"))
            heights.insert(h);
    }
    if (heights.isEmpty() && o.value("height").toInt() > 0)
        heights.insert(o.value("height").toInt());
    QList<int> hs = heights.values();
    std::sort(hs.begin(), hs.end(), std::greater<int>());

    auto add = [this](const QString &text, int id) {
        auto *r = new QRadioButton(text, this);
        m_group->addButton(r, id);
        m_options->addWidget(r);
        return r;
    };
    QRadioButton *first = add(QStringLiteral("Лучшее качество"), 0);
    QSet<int> shown;
    for (int h : hs) {
        // Округляем к привычным значениям: 2160, 1440, 1080, 720, 480, 360, 240, 144
        int label = h;
        for (int ref : {4320, 2160, 1440, 1080, 720, 480, 360, 240, 144})
            if (qAbs(h - ref) <= ref / 12) { label = ref; break; }
        if (shown.contains(label))
            continue;
        shown.insert(label);
        const QString name = label >= 2160 ? QStringLiteral("%1p (4K)").arg(label) : label >= 1440 ? QStringLiteral("%1p (2K)").arg(label)
                                                                                                   : QStringLiteral("%1p").arg(label);
        add(name, h);
    }
    add(QStringLiteral("Только звук (MP3)"), -100);
    first->setChecked(true);
    m_ok->setEnabled(true);
    adjustSize();

    // Миниатюра в фоне
    const QString url = m_item->thumbUrl;
    auto img = QSharedPointer<QImage>::create();
    Ui::runAsync(this, [url, img] { *img = grabImageUrl(url); }, [this, img] {
        if (img->isNull())
            return;
        m_item->thumb = *img;
        m_thumb->setPixmap(QPixmap::fromImage(img->scaled(m_thumb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    });
}

// ======================= DownloadCard =======================

DownloadCard::DownloadCard(const DownloadPtr &item, QWidget *parent)
    : QFrame(parent), m_item(item)
{
    setObjectName(QStringLiteral("TaskCard"));
    setAttribute(Qt::WA_StyledBackground);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(24, 16, 10, 16);
    row->setSpacing(18);

    m_thumb = new QLabel(this);
    m_thumb->setObjectName(QStringLiteral("Thumb"));
    m_thumb->setFixedSize(176, 99);
    m_thumb->setAlignment(Qt::AlignCenter);
    row->addWidget(m_thumb);

    auto *col = new QVBoxLayout;
    col->setSpacing(8);
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("FileName"));
    m_meta = new QLabel(this);
    m_meta->setObjectName(QStringLiteral("Muted"));
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1000);
    m_progress->setFixedHeight(6);
    m_state = new QLabel(this);
    m_state->setObjectName(QStringLiteral("StatusLabel"));
    m_state->setWordWrap(true);
    col->addWidget(m_title);
    col->addWidget(m_meta);
    col->addWidget(m_progress);
    col->addWidget(m_state);
    col->addStretch();
    row->addLayout(col, 1);

    auto *actions = new QVBoxLayout;
    m_pause = Ui::iconButton("pause", QStringLiteral("Пауза"), this, 22);
    m_open = new QPushButton(QStringLiteral("Открыть папку"), this);
    m_convert = new QPushButton(QStringLiteral("Конвертировать"), this);
    actions->addStretch();
    actions->addWidget(m_pause, 0, Qt::AlignHCenter);
    actions->addWidget(m_open);
    actions->addWidget(m_convert);
    actions->addStretch();
    row->addLayout(actions);
    auto *close = Ui::iconButton("close", QStringLiteral("Убрать из списка"), this, 14);
    row->addWidget(close, 0, Qt::AlignTop);

    connect(m_pause, &QToolButton::clicked, this, [this] { emit pauseResume(m_item); });
    connect(close, &QToolButton::clicked, this, [this] { emit removeRequested(m_item); });
    connect(m_open, &QPushButton::clicked, this, [this] {
#ifdef Q_OS_WIN
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(m_item->filePath)});
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_item->filePath).absolutePath()));
#endif
    });
    connect(m_convert, &QPushButton::clicked, this, [this] { emit convertRequested(m_item->filePath); });
    refresh();
}

void DownloadCard::refresh()
{
    const DownloadItem &d = *m_item;
    m_title->setText(fontMetrics().elidedText(d.title.isEmpty() ? d.url : d.title, Qt::ElideRight, 700));
    m_title->setToolTip(d.url);
    QStringList meta;
    if (!d.site.isEmpty()) meta << d.site;
    meta << (d.audioOnly ? QStringLiteral("MP3") : d.height > 0 ? QStringLiteral("%1p").arg(d.height) : QStringLiteral("лучшее качество"));
    if (d.duration > 0) meta << formatDuration(d.duration);
    if (d.total > 0) meta << formatSize(d.total);
    m_meta->setText(meta.join(QStringLiteral(" · ")));
    if (!d.thumb.isNull() && !m_thumb->pixmap())
        m_thumb->setPixmap(QPixmap::fromImage(d.thumb.scaled(m_thumb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    else if (d.thumb.isNull())
        m_thumb->setPixmap(Ui::icon("globe", true).pixmap(36, 36));

    m_progress->setValue(int(d.progress * 1000));
    m_progress->setVisible(d.status != DownloadItem::Done);
    m_open->setVisible(d.status == DownloadItem::Done);
    m_convert->setVisible(d.status == DownloadItem::Done);
    m_pause->setVisible(d.status != DownloadItem::Done);
    const bool canResume = d.status == DownloadItem::Paused || d.status == DownloadItem::Failed;
    m_pause->setIcon(Ui::icon(canResume ? "resume" : "pause", true));
    m_pause->setToolTip(canResume ? QStringLiteral("Продолжить") : QStringLiteral("Пауза"));

    switch (d.status) {
    case DownloadItem::Queued: m_state->setText(QStringLiteral("В очереди")); break;
    case DownloadItem::Paused: m_state->setText(QStringLiteral("Пауза · %1%").arg(int(d.progress * 100))); break;
    case DownloadItem::Failed: m_state->setText(QStringLiteral("Ошибка: ") + d.error); break;
    case DownloadItem::Done: m_state->setText(QStringLiteral("✔ Готово · ") + QFileInfo(d.filePath).fileName()); break;
    case DownloadItem::Downloading: {
        QString t = QStringLiteral("Загрузка %1%").arg(int(d.progress * 100));
        if (d.speed > 0) t += QStringLiteral(" · ") + speedText(d.speed);
        if (d.eta >= 0) t += QStringLiteral(" · осталось ") + formatDuration(d.eta);
        m_state->setText(t);
        break;
    }
    }
}

// ======================= DownloadPage =======================

DownloadPage::DownloadPage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildSubBar());

    m_stack = new QStackedWidget(this);
    // 0: пусто
    auto *empty = new QWidget(this);
    auto *el = new QVBoxLayout(empty);
    auto *pic = new QToolButton(empty);
    pic->setIcon(QIcon(QStringLiteral(":/icons/download-empty.svg")));
    pic->setIconSize(QSize(360, 220));
    pic->setAutoRaise(true);
    pic->setCursor(Qt::PointingHandCursor);
    pic->setStyleSheet(QStringLiteral("QToolButton{border:none;background:transparent;}"));
    auto *hint = new QLabel(QStringLiteral("Скопируйте ссылку на видео (YouTube, Rutube, VK, Одноклассники, Mail.ru, Дзен и др.\n"
                                           "или прямую ссылку на файл/поток) и нажмите «Вставить ссылку»"),
                            empty);
    hint->setAlignment(Qt::AlignCenter);
    hint->setObjectName(QStringLiteral("Muted"));
    el->addStretch();
    el->addWidget(pic, 0, Qt::AlignCenter);
    el->addSpacing(16);
    el->addWidget(hint, 0, Qt::AlignCenter);
    el->addStretch();
    connect(pic, &QToolButton::clicked, this, &DownloadPage::pasteFromClipboard);
    m_stack->addWidget(empty);

    auto makeList = [this](QVBoxLayout *&lay) {
        auto *scroll = new QScrollArea(this);
        scroll->setObjectName(QStringLiteral("TaskArea"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *w = new QWidget(scroll);
        lay = new QVBoxLayout(w);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
        lay->addStretch();
        scroll->setWidget(w);
        return scroll;
    };
    m_stack->addWidget(makeList(m_cardLayout));   // 1: активные
    m_stack->addWidget(makeList(m_doneLayout));   // 2: готовые
    root->addWidget(m_stack, 1);
    root->addWidget(buildBottomBar());

    if (Tools::ytdlp().isEmpty())
        m_status->setText(QStringLiteral("⚠ Модуль yt-dlp не найден — положите yt-dlp в папку tools рядом с программой"));
    updateView();
}

QWidget *DownloadPage::buildSubBar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("SubBar"));
    bar->setAttribute(Qt::WA_StyledBackground);
    bar->setFixedHeight(52);
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(30, 0, 30, 0);

    auto *paste = new QToolButton(bar);
    paste->setObjectName(QStringLiteral("FlatTool"));
    paste->setText(QStringLiteral("Вставить ссылку"));
    paste->setIcon(Ui::icon("add", true));
    paste->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    paste->setPopupMode(QToolButton::MenuButtonPopup);
    paste->setCursor(Qt::PointingHandCursor);
    auto *menu = new QMenu(paste);
    menu->addAction(QStringLiteral("Вставить из буфера обмена"), this, &DownloadPage::pasteFromClipboard);
    menu->addAction(QStringLiteral("Ввести ссылки вручную…"), this, &DownloadPage::enterLinks);
    paste->setMenu(menu);
    connect(paste, &QToolButton::clicked, this, &DownloadPage::pasteFromClipboard);

    auto *rec = new QToolButton(bar);
    rec->setObjectName(QStringLiteral("FlatTool"));
    rec->setText(QStringLiteral("Записать видео"));
    rec->setIcon(Ui::icon("record", true));
    rec->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    rec->setCursor(Qt::PointingHandCursor);
    connect(rec, &QToolButton::clicked, this, [this] {
        Ui::message(this, QStringLiteral("Запись видео"), QStringLiteral("Запись экрана появится в одной из следующих версий."));
    });

    m_segActive = new QPushButton(QStringLiteral("Загружается"), bar);
    m_segActive->setObjectName(QStringLiteral("SegLeft"));
    m_segDone = new QPushButton(QStringLiteral("Готово"), bar);
    m_segDone->setObjectName(QStringLiteral("SegRight"));
    for (QPushButton *b : {m_segActive, m_segDone}) {
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, &DownloadPage::updateView);
    }
    m_segActive->setChecked(true);

    auto *modeLbl = new QLabel(QStringLiteral("Скачать, затем конвертировать"), bar);
    m_convertMode = new QCheckBox(bar);
    m_convertMode->setToolTip(QStringLiteral("После загрузки файл сразу конвертируется в формат, выбранный в разделе «Конвертировать»"));
    QSettings s;
    m_convertMode->setChecked(s.value("download/convertMode", false).toBool());
    connect(m_convertMode, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("download/convertMode", on); });

    lay->addWidget(paste);
    lay->addSpacing(20);
    lay->addWidget(rec);
    lay->addStretch();
    lay->addWidget(m_segActive);
    lay->addWidget(m_segDone);
    lay->addStretch();
    lay->addWidget(modeLbl);
    lay->addWidget(m_convertMode);
    return bar;
}

QWidget *DownloadPage::buildBottomBar()
{
    auto *outer = new QWidget(this);
    auto *ol = new QVBoxLayout(outer);
    ol->setContentsMargins(0, 0, 0, 0);
    ol->setSpacing(0);
    m_status = new QLabel(outer);
    m_status->setObjectName(QStringLiteral("StatusLabel"));
    m_status->setContentsMargins(30, 4, 30, 4);
    m_status->setWordWrap(true);
    ol->addWidget(m_status);

    auto *bar = new QWidget(outer);
    bar->setObjectName(QStringLiteral("BottomBar"));
    bar->setAttribute(Qt::WA_StyledBackground);
    bar->setFixedHeight(72);
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(30, 0, 40, 0);
    lay->setSpacing(12);

    QSettings s;
    m_fast = Ui::iconButton("bolt", QString(), bar, 22);
    m_fast->setCheckable(true);
    m_fast->setChecked(s.value("download/fast", true).toBool());
    auto *fastLbl = new QLabel(QStringLiteral("Быстрая загрузка"), bar);
    auto updFast = [this] {
        m_fast->setIcon(Ui::icon("bolt", m_fast->isChecked()));
        m_fast->setToolTip(m_fast->isChecked() ? QStringLiteral("Загрузка в несколько потоков: ВКЛ") : QStringLiteral("Загрузка в несколько потоков: ВЫКЛ"));
    };
    updFast();
    connect(m_fast, &QToolButton::toggled, this, [updFast](bool on) { QSettings().setValue("download/fast", on); updFast(); });

    auto *loginLbl = new QLabel(QStringLiteral("Вход на сайты:"), bar);
    m_browser = new QComboBox(bar);
    m_browser->setToolTip(QStringLiteral("Для видео, доступных только после входа: программа возьмёт вход из выбранного браузера.\n"
                                         "Надёжнее всего работает Firefox. Браузер лучше закрыть перед загрузкой."));
    m_browser->addItem(QStringLiteral("Не использовать"), QString());
    for (const QString &b : {"firefox", "chrome", "edge", "opera", "brave", "vivaldi", "chromium"})
        m_browser->addItem(b.left(1).toUpper() + b.mid(1), b);
    m_browser->setCurrentIndex(qMax(0, m_browser->findData(s.value("download/browser").toString())));
    connect(m_browser, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { QSettings().setValue("download/browser", m_browser->currentData()); });

    auto *outLbl = new QLabel(QStringLiteral("Папка:"), bar);
    m_outDir = new QComboBox(bar);
    m_outDir->setMinimumWidth(300);
    m_outDir->addItem(downloadDir());
    m_outDir->addItem(QStringLiteral("Выбрать папку…"));
    connect(m_outDir, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        if (i != m_outDir->count() - 1)
            return;
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Папка для загрузок"), downloadDir());
        if (!d.isEmpty()) {
            QSettings().setValue("download/dir", d);
            m_outDir->setItemText(0, d);
        }
        m_outDir->setCurrentIndex(0);
    });
    auto *openDir = Ui::iconButton("folder", QStringLiteral("Открыть папку загрузок"), bar, 22);
    openDir->setIcon(Ui::icon("folder", true));
    connect(openDir, &QToolButton::clicked, this, [this] {
        QDir().mkpath(outputDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(outputDir()));
    });

    m_startAll = Ui::primaryButton(QStringLiteral("Начать все"), bar);
    m_pauseAll = new QPushButton(QStringLiteral("Пауза все"), bar);
    m_pauseAll->setObjectName(QStringLiteral("DarkButton"));
    for (QPushButton *b : {m_startAll, m_pauseAll}) {
        b->setMinimumWidth(150);
        b->setMinimumHeight(46);
    }
    connect(m_startAll, &QPushButton::clicked, this, [this] {
        for (const DownloadPtr &d : m_items)
            if (d->status == DownloadItem::Paused || d->status == DownloadItem::Failed)
                d->status = DownloadItem::Queued;
        for (DownloadCard *c : m_cards) c->refresh();
        startNext();
    });
    connect(m_pauseAll, &QPushButton::clicked, this, [this] {
        for (const DownloadPtr &d : m_items)
            if (d->status == DownloadItem::Queued)
                d->status = DownloadItem::Paused;
        if (m_current)
            pauseResume(m_current);
        for (DownloadCard *c : m_cards) c->refresh();
    });

    lay->addWidget(m_fast);
    lay->addWidget(fastLbl);
    lay->addSpacing(18);
    lay->addWidget(loginLbl);
    lay->addWidget(m_browser);
    lay->addSpacing(18);
    lay->addWidget(outLbl);
    lay->addWidget(m_outDir, 1);
    lay->addWidget(openDir);
    lay->addStretch();
    lay->addWidget(m_startAll);
    lay->addWidget(m_pauseAll);
    ol->addWidget(bar);
    return outer;
}

QString DownloadPage::outputDir() const
{
    return m_outDir->itemText(0);
}

QStringList DownloadPage::baseArgs() const
{
    QStringList a = {"--no-playlist", "--no-colors", "--newline"};
    const QString ff = Tools::ffmpeg();
    if (!ff.isEmpty())
        a << "--ffmpeg-location" << QFileInfo(ff).absolutePath();
    const QString deno = Tools::deno();
    if (!deno.isEmpty())
        a << "--js-runtimes" << QStringLiteral("deno:") + deno;
    const QString browser = m_browser->currentData().toString();
    if (!browser.isEmpty())
        a << "--cookies-from-browser" << browser;
    return a;
}

void DownloadPage::pasteFromClipboard()
{
    const QString text = QApplication::clipboard()->text().trimmed();
    const QRegularExpression re(QStringLiteral("(https?://\\S+)"));
    auto it = re.globalMatch(text);
    QStringList urls;
    while (it.hasNext())
        urls << it.next().captured(1);
    if (urls.isEmpty()) {
        enterLinks();
        return;
    }
    for (const QString &u : urls)
        addUrl(u);
}

void DownloadPage::enterLinks()
{
    FramedDialog d(this);
    d.setWindowTitle(QStringLiteral("Ссылки на видео"));
    d.setMinimumWidth(560);
    auto *lay = d.contentLayout();
    lay->setContentsMargins(24, 18, 24, 18);
    lay->addWidget(new QLabel(QStringLiteral("Вставьте одну или несколько ссылок, каждую с новой строки:"), &d));
    auto *edit = new QPlainTextEdit(&d);
    edit->setMinimumHeight(140);
    lay->addWidget(edit);
    auto *btns = new QHBoxLayout;
    btns->addStretch();
    auto *n = new QPushButton(QStringLiteral("Отмена"), &d);
    auto *y = Ui::primaryButton(QStringLiteral("Добавить"), &d);
    btns->addWidget(n);
    btns->addWidget(y);
    lay->addLayout(btns);
    connect(n, &QPushButton::clicked, &d, &QDialog::reject);
    connect(y, &QPushButton::clicked, &d, &QDialog::accept);
    if (d.exec() != QDialog::Accepted)
        return;
    for (const QString &line : edit->toPlainText().split('\n', Qt::SkipEmptyParts)) {
        const QString u = line.trimmed();
        if (u.startsWith(QLatin1String("http")))
            addUrl(u);
    }
}

void DownloadPage::addUrl(const QString &url)
{
    if (Tools::ytdlp().isEmpty()) {
        Ui::message(this, QStringLiteral("Загрузка видео"), QStringLiteral("Модуль yt-dlp не найден. Положите yt-dlp в папку tools рядом с программой."));
        return;
    }
    AnalyzeDialog dlg(url, baseArgs(), window());
    if (dlg.exec() != QDialog::Accepted)
        return;
    addItem(dlg.result());
}

void DownloadPage::addItem(const DownloadPtr &item)
{
    item->id = m_nextId++;
    item->status = DownloadItem::Queued;
    m_items << item;
    auto *card = new DownloadCard(item, this);
    m_cards.insert(item->id, card);
    m_cardLayout->insertWidget(m_cardLayout->count() - 1, card);
    connect(card, &DownloadCard::pauseResume, this, &DownloadPage::pauseResume);
    connect(card, &DownloadCard::removeRequested, this, &DownloadPage::removeItem);
    connect(card, &DownloadCard::convertRequested, this, [this](const QString &f) { emit convertRequested(f, false); });
    m_segActive->setChecked(true);
    updateView();
    startNext();
}

void DownloadPage::startNext()
{
    if (m_proc)
        return;
    for (const DownloadPtr &d : m_items) {
        if (d->status == DownloadItem::Queued) {
            startItem(d);
            return;
        }
    }
    updateView();
}

void DownloadPage::startItem(const DownloadPtr &item)
{
    m_current = item;
    item->status = DownloadItem::Downloading;
    item->error.clear();
    m_stderr.clear();
    m_pausing = false;
    QDir().mkpath(outputDir());

    QStringList a = baseArgs();
    a << "--no-simulate" << "--progress"
      << "--progress-template" << "download:ARISTO|%(progress.downloaded_bytes)s|%(progress.total_bytes)s|%(progress.total_bytes_estimate)s|%(progress.speed)s|%(progress.eta)s"
      << "--print" << "after_move:ARISTOFILE|%(filepath)s"
      << "-P" << outputDir()
      << "-o" << "%(title).120B [%(id)s].%(ext)s";
    if (m_fast->isChecked())
        a << "--concurrent-fragments" << "8";
    if (item->audioOnly) {
        a << "-f" << "ba/b" << "-x" << "--audio-format" << "mp3" << "--audio-quality" << "0";
    } else {
        a << "-f" << "bv*+ba/b" << "--merge-output-format" << "mp4/mkv";
        a << "-S" << (item->height > 0 ? QStringLiteral("res:%1,ext:mp4:m4a").arg(item->height) : QStringLiteral("ext:mp4:m4a"));
    }
    a << item->url;

    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &DownloadPage::onOutput);
    connect(m_proc, &QProcess::readyReadStandardError, this, [this] {
        m_stderr += QString::fromUtf8(m_proc->readAllStandardError());
        if (m_stderr.size() > 6000) m_stderr = m_stderr.right(6000);
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &DownloadPage::onFinished);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            onFinished(-1, QProcess::CrashExit);
    });
    m_proc->start(Tools::ytdlp(), a);
    if (DownloadCard *c = m_cards.value(item->id))
        c->refresh();
    if (item->thumb.isNull())
        loadThumb(item);
}

void DownloadPage::loadThumb(const DownloadPtr &item)
{
    const QString url = item->thumbUrl;
    if (url.isEmpty())
        return;
    auto img = QSharedPointer<QImage>::create();
    Ui::runAsync(this, [url, img] { *img = grabImageUrl(url); }, [this, item, img] {
        item->thumb = *img;
        if (DownloadCard *c = m_cards.value(item->id))
            c->refresh();
    });
}

void DownloadPage::onOutput()
{
    while (m_proc && m_proc->canReadLine()) {
        const QString line = QString::fromUtf8(m_proc->readLine()).trimmed();
        if (!m_current)
            continue;
        if (line.startsWith(QLatin1String("ARISTO|"))) {
            const QStringList p = line.split('|');
            if (p.size() < 6)
                continue;
            const qint64 done = p[1].toLongLong();
            qint64 total = p[2].toLongLong();
            if (total <= 0)
                total = qint64(p[3].toDouble());
            m_current->done = done;
            if (total > 0) {
                m_current->total = total;
                m_current->progress = qBound(0.0, double(done) / total, 1.0);
            }
            m_current->speed = p[4].toDouble();
            m_current->eta = p[5] == QLatin1String("NA") ? -1 : p[5].toInt();
            if (DownloadCard *c = m_cards.value(m_current->id))
                c->refresh();
        } else if (line.startsWith(QLatin1String("ARISTOFILE|"))) {
            m_current->filePath = line.mid(11);
        }
    }
}

void DownloadPage::onFinished(int code, QProcess::ExitStatus st)
{
    if (!m_proc)
        return;
    onOutput();
    m_proc->deleteLater();
    m_proc = nullptr;
    DownloadPtr item = m_current;
    m_current.reset();
    if (!item)
        return;

    if (m_pausing) {
        item->status = DownloadItem::Paused;
    } else if (st == QProcess::NormalExit && code == 0 && !item->filePath.isEmpty()) {
        item->status = DownloadItem::Done;
        item->progress = 1;
        item->total = QFileInfo(item->filePath).size();
        if (DownloadCard *c = m_cards.take(item->id)) {
            m_cardLayout->removeWidget(c);
            m_doneLayout->insertWidget(0, c);
            m_cards.insert(item->id, c);
        }
        m_segDone->setText(QStringLiteral("Готово (%1)").arg(m_doneLayout->count() - 1));
        if (m_convertMode->isChecked()) {
            emit convertRequested(item->filePath, true);
            m_status->setText(QStringLiteral("«%1» скачано и отправлено на конвертацию").arg(item->title));
        } else {
            m_status->setText(QStringLiteral("Скачано: %1").arg(item->title));
        }
    } else {
        item->status = DownloadItem::Failed;
        QStringList lines;
        for (const QString &l : m_stderr.split('\n', Qt::SkipEmptyParts))
            if (l.contains(QLatin1String("ERROR")))
                lines << l;
        QString err = lines.isEmpty() ? (code == -1 ? QStringLiteral("yt-dlp не запустился") : QStringLiteral("загрузка прервана")) : lines.last();
        err.remove(QStringLiteral("ERROR: "));
        if (err.contains(QLatin1String("DRM"), Qt::CaseInsensitive))
            err = QStringLiteral("видео защищено DRM — такие видео программа не скачивает");
        item->error = err;
    }
    if (DownloadCard *c = m_cards.value(item->id))
        c->refresh();
    updateView();
    startNext();
}

void DownloadPage::pauseResume(const DownloadPtr &item)
{
    if (item == m_current && m_proc) {
        m_pausing = true;
        m_proc->kill();     // недокачанная часть (.part) сохраняется, yt-dlp продолжит с того же места
        return;
    }
    if (item->status == DownloadItem::Paused || item->status == DownloadItem::Failed)
        item->status = DownloadItem::Queued;
    else if (item->status == DownloadItem::Queued)
        item->status = DownloadItem::Paused;
    if (DownloadCard *c = m_cards.value(item->id))
        c->refresh();
    startNext();
}

void DownloadPage::removeItem(const DownloadPtr &item)
{
    if (item == m_current && m_proc) {
        m_pausing = true;
        m_proc->kill();
        m_proc->waitForFinished(3000);
    }
    m_items.removeAll(item);
    if (DownloadCard *c = m_cards.take(item->id))
        c->deleteLater();
    updateView();
}

void DownloadPage::updateView()
{
    bool anyActive = false;
    for (const DownloadPtr &d : m_items)
        if (d->status != DownloadItem::Done)
            anyActive = true;
    if (m_segDone->isChecked())
        m_stack->setCurrentIndex(2);
    else
        m_stack->setCurrentIndex(anyActive ? 1 : 0);
    m_startAll->setEnabled(anyActive);
    m_pauseAll->setEnabled(m_proc != nullptr);
}
