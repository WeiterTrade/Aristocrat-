#include "DownloadPage.h"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include "core/I18n.h"
#include "Widgets.h"
#include "ChaptersDialog.h"
#ifdef HAVE_WEBENGINE
#include "BrowserWindow.h"
#include "PageGrabber.h"
#endif
#include "core/Release.h"
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
#include <QDateTime>
#include <QAbstractButton>
#include <QDir>
#include <QTimer>
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

static QString safeFileName(QString t);

static QString speedText(double bps)
{
    if (bps <= 0)
        return {};
    if (bps > 1024 * 1024)
        return QString::number(bps / 1024 / 1024, 'f', 1).replace('.', ',') + T(" МБ/с");
    return QString::number(bps / 1024, 'f', 0) + T(" КБ/с");
}

// Папка загрузок. В папке программы ничего не храним: если папка совпадает с ней или лежит внутри — «Загрузки/Aristocrat Загрузки»
static QString downloadDir()
{
    QSettings s;
    QString def = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (def.isEmpty())
        def = QDir::homePath();
    def += QStringLiteral("/Aristocrat Загрузки");
    const QString dir = s.value("download/dir", def).toString();
    return Tools::insideAppDir(dir) ? def : dir;
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

AnalyzeDialog::AnalyzeDialog(const QString &url, const QStringList &baseArgs, const QStringList &cookieArgs, QWidget *parent,
                             const QStringList &headers)
    : FramedDialog(parent), m_item(DownloadPtr::create()), m_base(baseArgs), m_cookies(headers.isEmpty() ? cookieArgs : QStringList()), m_headers(headers)
{
    setWindowTitle(T("Загрузка видео"));
    setMinimumWidth(640);
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
    m_title = new QLabel(this);
    m_title->setWordWrap(true);
    m_title->setStyleSheet(QStringLiteral("font-size:11pt;"));
    m_title->setText(fontMetrics().elidedText(url, Qt::ElideMiddle, 380));
    m_title->setToolTip(url);
    m_state = new QLabel(T("Анализ ссылки…"), this);
    m_state->setObjectName(QStringLiteral("StatusLabel"));
    m_state->setWordWrap(true);
    txt->addWidget(m_title);
    txt->addWidget(m_state);
    txt->addStretch();
    top->addWidget(m_thumb);
    top->addSpacing(12);
    top->addLayout(txt, 1);
    root->addLayout(top);

    m_details = new QPlainTextEdit(this);
    m_details->setReadOnly(true);
    m_details->setMaximumHeight(130);
    m_details->hide();
    root->addWidget(m_details);

    auto *qLbl = new QLabel(T("Качество:"), this);
    qLbl->setObjectName(QStringLiteral("Muted"));
    root->addWidget(qLbl);
    m_options = new QVBoxLayout;
    m_group = new QButtonGroup(this);
    root->addLayout(m_options);

    auto *btns = new QHBoxLayout;
    btns->addStretch();
    auto *cancel = new QPushButton(T("Отмена"), this);
    m_ok = Ui::primaryButton(T("Скачать"), this);
    m_ok->setEnabled(false);
    m_ok->setMinimumWidth(140);
#ifdef HAVE_WEBENGINE
    m_browserBtn = Ui::primaryButton(T("Открыть во встроенном браузере"), this);
    m_browserBtn->hide();
    btns->addWidget(m_browserBtn);
    connect(m_browserBtn, &QPushButton::clicked, this, [this] { done(2); });
#endif
    btns->addWidget(cancel);
    btns->addWidget(m_ok);
    root->addLayout(btns);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_ok, &QPushButton::clicked, this, [this] {
        const int id = m_group->checkedId();
        m_item->audioOnly = id == -100;
        m_item->height = id > 0 ? id : 0;
        m_item->noCookies = !m_usedCookies;
        if (!m_headers.isEmpty()) {
            m_item->external = true;
            m_item->headers = m_headers;
        }
        accept();
    });

    m_proc = new QProcess(this);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code) { onAnalyzed(code); });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            m_state->setText(T("Не удалось запустить модуль загрузки (yt-dlp)."));
    });
    run(!m_cookies.isEmpty());
}

void AnalyzeDialog::run(bool withCookies)
{
    m_usedCookies = withCookies;
    QStringList args = m_base;
    if (withCookies)
        args << m_cookies;
    for (const QString &h : m_headers)
        args << "--add-header" << h;
    args << "-J" << "--no-warnings" << m_item->url;
    m_proc->setProcessEnvironment(Tools::utf8Env());
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
        const QString full = QString::fromUtf8(m_proc->readAllStandardError()).trimmed();
        // Не получилось прочитать вход из браузера — пробуем без него
        if (m_usedCookies && full.contains(QLatin1String("cookie"), Qt::CaseInsensitive)) {
            m_state->setText(T("Не удалось взять вход из браузера (закройте браузер или выберите другой). Пробую без входа…"));
            run(false);
            return;
        }
        QStringList errs;
        for (const QString &l : full.split('\n', Qt::SkipEmptyParts))
            if (l.startsWith(QLatin1String("ERROR")))
                errs << l.mid(l.indexOf(':') + 1).trimmed();
        QString err = errs.isEmpty() ? T("Видео по этой ссылке не найдено.") : errs.last();
        QString hint;
        if (err.contains(QLatin1String("Unsupported URL"), Qt::CaseInsensitive))
            hint = T("Сайт не поддерживается напрямую. Откройте страницу во встроенном браузере программы: "
                     "запустите видео — и программа сама найдёт поток для загрузки.");
        else if (err.contains(QLatin1String("Sign in"), Qt::CaseInsensitive) || err.contains(QLatin1String("login"), Qt::CaseInsensitive)
                 || err.contains(QLatin1String("bot"), Qt::CaseInsensitive))
            hint = T("Сайт требует входа. Внизу раздела выберите «Вход на сайты» — браузер, в котором вы вошли на сайт (лучше Firefox), "
                                  "и закройте этот браузер перед загрузкой.");
        else if (err.contains(QLatin1String("DRM"), Qt::CaseInsensitive))
            hint = T("Видео защищено DRM — такие видео программа не скачивает.");
        else
            hint = T("Если ошибка повторяется, обновите модуль загрузки: ☰ → Настройки → Загрузка → «Обновить».");
#ifdef HAVE_WEBENGINE
        // Сайт не поддерживается напрямую — программа поищет видео на странице сама
        if (m_headers.isEmpty() && err.contains(QLatin1String("Unsupported URL"), Qt::CaseInsensitive)) {
            done(3);
            return;
        }
#endif
        m_state->setText(T("Не получилось. ") + hint);
        if (m_headers.isEmpty() && m_browserBtn)
            m_browserBtn->show();
        m_details->setPlainText(full.isEmpty() ? err : full);
        m_details->show();
        adjustSize();
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
    QRadioButton *first = add(T("Лучшее качество"), 0);
    QSet<int> shown;
    for (int h : hs) {
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
    add(T("Только звук (MP3)"), -100);
    first->setChecked(true);
    m_ok->setEnabled(true);
    adjustSize();
    if (m_autoPick == -100) {   // только звук (дорожка для релиза)
        if (QAbstractButton *b = m_group->button(-100))
            b->setChecked(true);
        QTimer::singleShot(300, m_ok, &QPushButton::click);
    } else if (m_autoPick > 0 && !hs.isEmpty()) {
        int pick = 0;
        for (int h : hs)   // точно нужная (с допуском) или ближайшая выше
            if (h >= m_autoPick - m_autoPick / 12 && (pick == 0 || h < pick))
                pick = h;
        if (pick == 0)
            pick = hs.first();   // выше нет — лучшая из имеющихся (потом улучшится до 720p)
        if (QAbstractButton *b = m_group->button(pick))
            b->setChecked(true);
        QTimer::singleShot(300, m_ok, &QPushButton::click);
    }

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
    m_pause = Ui::iconButton("pause", T("Пауза"), this, 22);
    m_open = new QPushButton(T("Открыть папку"), this);
    m_convert = new QPushButton(T("Конвертировать"), this);
    m_chapters = new QPushButton(T("Главы"), this);
    m_chapters->setToolTip(T("Вставить главы (сцены) с сайта в этот файл"));
    actions->addStretch();
    actions->addWidget(m_pause, 0, Qt::AlignHCenter);
    actions->addWidget(m_open);
    actions->addWidget(m_convert);
    m_release = new QPushButton(T("Релиз"), this);
    m_release->setToolTip(T("Adult Empire Менеджер: папка с NFO, обложками и фото актрис"));
    auto *extra = new QHBoxLayout;
    extra->setSpacing(4);
    extra->addWidget(m_chapters);
    extra->addWidget(m_release);
    actions->addLayout(extra);
    actions->addStretch();
    row->addLayout(actions);
    auto *close = Ui::iconButton("close", T("Убрать из списка"), this, 14);
    row->addWidget(close, 0, Qt::AlignTop);

    connect(m_pause, &QToolButton::clicked, this, [this] { emit pauseResume(m_item); });
    connect(close, &QToolButton::clicked, this, [this] { emit removeRequested(m_item); });
    connect(m_open, &QPushButton::clicked, this, [this] { Ui::showInFolder(m_item->filePath, downloadDir()); });
    connect(m_convert, &QPushButton::clicked, this, [this] { emit convertRequested(m_item->filePath); });
    connect(m_chapters, &QPushButton::clicked, this, [this] { emit chaptersRequested(m_item->filePath, m_item->pageUrl); });
    connect(m_release, &QPushButton::clicked, this, [this] { emit releaseRequested(m_item->filePath, m_item->pageUrl); });
    refresh();
}

void DownloadCard::refresh()
{
    const DownloadItem &d = *m_item;
    m_title->setText(fontMetrics().elidedText(d.title.isEmpty() ? d.url : d.title, Qt::ElideRight, 700));
    m_title->setToolTip(d.url);
    QStringList meta;
    if (!d.site.isEmpty()) meta << d.site;
    meta << (d.audioOnly ? QStringLiteral("MP3") : d.height > 0 ? QStringLiteral("%1p").arg(d.height) : T("лучшее качество"));
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
    m_chapters->setVisible(d.status == DownloadItem::Done && !d.pageUrl.isEmpty());
    m_release->setVisible(d.status == DownloadItem::Done && d.pageUrl.contains(QLatin1String("adultempire"), Qt::CaseInsensitive));
    m_pause->setVisible(d.status != DownloadItem::Done);
    const bool canResume = d.status == DownloadItem::Paused || d.status == DownloadItem::Failed;
    m_pause->setIcon(Ui::icon(canResume ? "resume" : "pause", true));
    m_pause->setToolTip(canResume ? T("Продолжить") : T("Пауза"));

    switch (d.status) {
    case DownloadItem::Queued: m_state->setText(T("В очереди")); break;
    case DownloadItem::Paused:
        m_state->setText(d.retryWait ? T("Ждёт связи · %1% · %2").arg(int(d.progress * 100)).arg(d.error)
                                     : T("Пауза · %1%").arg(int(d.progress * 100)));
        break;
    case DownloadItem::Failed: m_state->setText(T("Ошибка: ") + d.error); break;
    case DownloadItem::Done: m_state->setText(T("✔ Готово · ") + QFileInfo(d.filePath).fileName()); break;
    case DownloadItem::Downloading: {
        QString t = T("Загрузка %1%").arg(int(d.progress * 100));
        if (d.speed > 0) t += QStringLiteral(" · ") + speedText(d.speed);
        if (d.eta >= 0) t += T(" · осталось ") + formatDuration(d.eta);
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
    pic->setIcon(Ui::icon(QStringLiteral("download-empty")));
    pic->setIconSize(QSize(360, 220));
    pic->setAutoRaise(true);
    pic->setCursor(Qt::PointingHandCursor);
    pic->setStyleSheet(QStringLiteral("QToolButton{border:none;background:transparent;}"));
    auto *hint = new QLabel(T("Скопируйте ссылку на видео (YouTube, Rutube, VK, Одноклассники, Mail.ru, Дзен и др.\n"
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

    loadQueue();
    if (Tools::ytdlp().isEmpty())
        m_status->setText(T("⚠ Модуль yt-dlp не найден — положите yt-dlp в папку tools рядом с программой"));
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
    paste->setText(T("Вставить ссылку"));
    paste->setIcon(Ui::icon("add", true));
    paste->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    paste->setPopupMode(QToolButton::MenuButtonPopup);
    paste->setCursor(Qt::PointingHandCursor);
    auto *menu = new QMenu(paste);
    menu->addAction(T("Вставить из буфера обмена"), this, &DownloadPage::pasteFromClipboard);
    menu->addAction(T("Ввести ссылки вручную…"), this, &DownloadPage::enterLinks);
    paste->setMenu(menu);
    connect(paste, &QToolButton::clicked, this, &DownloadPage::pasteFromClipboard);

    auto *rec = new QToolButton(bar);
    rec->setObjectName(QStringLiteral("FlatTool"));
    rec->setText(T("Записать видео"));
    rec->setIcon(Ui::icon("record", true));
    rec->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    rec->setCursor(Qt::PointingHandCursor);
    connect(rec, &QToolButton::clicked, this, [this] {
        Ui::message(this, T("Запись видео"), T("Запись экрана появится в одной из следующих версий."));
    });

    m_segActive = new QPushButton(T("Загружается"), bar);
    m_segActive->setObjectName(QStringLiteral("SegLeft"));
    m_segDone = new QPushButton(T("Готово"), bar);
    m_segDone->setObjectName(QStringLiteral("SegRight"));
    for (QPushButton *b : {m_segActive, m_segDone}) {
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, &DownloadPage::updateView);
    }
    m_segActive->setChecked(true);

    auto *modeLbl = new QLabel(T("Скачать, затем конвертировать"), bar);
    m_convertMode = new QCheckBox(bar);
    m_convertMode->setToolTip(T("После загрузки файл сразу конвертируется в формат, выбранный в разделе «Конвертировать»"));
    QSettings s;
    m_convertMode->setChecked(s.value("download/convertMode", false).toBool());
    connect(m_convertMode, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("download/convertMode", on); });

    auto *ext = new QToolButton(bar);
    ext->setObjectName(QStringLiteral("FlatTool"));
    ext->setText(T("Браузер"));
    ext->setToolTip(T("Встроенный браузер: откройте сайт, запустите видео — и скачайте его кнопкой «Скачать»"));
    ext->setIcon(Ui::icon("globe", true));
    ext->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    ext->setCursor(Qt::PointingHandCursor);
    connect(ext, &QToolButton::clicked, this, [this] { openBrowser(); });

    lay->addWidget(paste);
    lay->addSpacing(20);
    lay->addWidget(rec);
    lay->addSpacing(20);
    lay->addWidget(ext);
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
    auto *fastLbl = new QLabel(T("Быстрая загрузка"), bar);
    auto updFast = [this] {
        m_fast->setIcon(Ui::icon("bolt", m_fast->isChecked()));
        m_fast->setToolTip(m_fast->isChecked() ? T("Загрузка в несколько потоков: ВКЛ") : T("Загрузка в несколько потоков: ВЫКЛ"));
    };
    updFast();
    connect(m_fast, &QToolButton::toggled, this, [updFast](bool on) { QSettings().setValue("download/fast", on); updFast(); });

    auto *loginLbl = new QLabel(T("Вход на сайты:"), bar);
    m_browser = new QComboBox(bar);
    m_browser->setToolTip(T("Для видео, доступных только после входа: программа возьмёт вход из выбранного браузера.\n"
                                         "Надёжнее всего работает Firefox. Браузер лучше закрыть перед загрузкой."));
    m_browser->addItem(T("Не использовать"), QString());
    m_browser->addItem(T("Яндекс Браузер"), QStringLiteral("yandex"));
    for (const QString &b : {"firefox", "chrome", "edge", "opera", "brave", "vivaldi", "chromium"})
        m_browser->addItem(b.left(1).toUpper() + b.mid(1), b);
    m_browser->setCurrentIndex(qMax(0, m_browser->findData(s.value("download/browser").toString())));
    connect(m_browser, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { QSettings().setValue("download/browser", m_browser->currentData()); });

    auto *outLbl = new QLabel(T("Папка:"), bar);
    m_outDir = new QComboBox(bar);
    m_outDir->setMinimumWidth(300);
    m_outDir->addItem(downloadDir());
    m_outDir->addItem(T("Выбрать папку…"));
    connect(m_outDir, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        if (i != m_outDir->count() - 1)
            return;
        const QString d = QFileDialog::getExistingDirectory(this, T("Папка для загрузок"), downloadDir());
        if (!d.isEmpty()) {
            QSettings().setValue("download/dir", d);
            m_outDir->setItemText(0, d);
        }
        m_outDir->setCurrentIndex(0);
    });
    auto *openDir = Ui::iconButton("folder", T("Открыть папку загрузок"), bar, 22);
    openDir->setIcon(Ui::icon("folder", true));
    connect(openDir, &QToolButton::clicked, this, [this] {
        QDir().mkpath(outputDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(outputDir()));
    });

    m_startAll = Ui::primaryButton(T("Начать все"), bar);
    m_pauseAll = new QPushButton(T("Пауза все"), bar);
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
        for (const DownloadPtr &d : m_items)
            if (m_procs.contains(d->id))
                pauseResume(d);
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

QStringList DownloadPage::cookieArgs(const QString &browser)
{
    if (browser.isEmpty())
        return {};
    if (browser == QLatin1String("yandex")) {
        // Яндекс Браузер построен на Chromium: указываем yt-dlp путь к его профилю
#ifdef Q_OS_WIN
        const QString local = QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA"));
        const QString profile = local + QStringLiteral("/Yandex/YandexBrowser/User Data/Default");
#else
        const QString profile = QDir::homePath() + QStringLiteral("/.config/yandex-browser/Default");
#endif
        return {"--cookies-from-browser", QStringLiteral("chromium:") + profile};
    }
    return {"--cookies-from-browser", browser};
}

QStringList DownloadPage::baseArgs(bool withCookies) const
{
    QStringList a = {"--no-playlist", "--no-colors", "--newline"};
    const QString ff = Tools::ffmpeg();
    if (!ff.isEmpty())
        a << "--ffmpeg-location" << QFileInfo(ff).absolutePath();
    const QString deno = Tools::deno();
    if (!deno.isEmpty())
        a << "--js-runtimes" << QStringLiteral("deno:") + deno;
    if (withCookies)
        a << cookieArgs(m_browser->currentData().toString());
    return a;
}

void DownloadPage::reloadSettings()
{
    m_outDir->setItemText(0, downloadDir());
    startNext();
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
    d.setWindowTitle(T("Ссылки на видео"));
    d.setMinimumWidth(560);
    auto *lay = d.contentLayout();
    lay->setContentsMargins(24, 18, 24, 18);
    lay->addWidget(new QLabel(T("Вставьте одну или несколько ссылок, каждую с новой строки:"), &d));
    auto *edit = new QPlainTextEdit(&d);
    edit->setMinimumHeight(140);
    lay->addWidget(edit);
    auto *btns = new QHBoxLayout;
    btns->addStretch();
    auto *n = new QPushButton(T("Отмена"), &d);
    auto *y = Ui::primaryButton(T("Добавить"), &d);
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
        Ui::message(this, T("Загрузка видео"), T("Модуль загрузки (yt-dlp) не найден в папке tools рядом с программой."));
        return;
    }
#ifdef HAVE_WEBENGINE
    // Сайты по подписке (Adult Empire): вход в аккаунт и поиск видео без окна браузера
    if (Sites::needsLogin(QUrl(url))) {
        grabAndAdd(url, true);
        return;
    }
#endif
    QSettings s;
    if (s.value("download/oneClick", false).toBool()) {
        // Быстрый режим: без окна выбора, качество из настроек
        auto item = DownloadPtr::create();
        item->url = url;
        item->audioOnly = s.value("download/oneClickType", "video").toString() == QLatin1String("audio");
        item->height = s.value("download/oneClickQuality", 0).toInt();
        addItem(item);
        return;
    }
    AnalyzeDialog dlg(url, baseArgs(false), cookieArgs(m_browser->currentData().toString()), window());
    const int r = dlg.exec();
    if (r == 2) {             // открыть во встроенном браузере
        openBrowser(url);
        return;
    }
    if (r == 3) {             // сайт не поддерживается напрямую — ищем видео на странице сами
        grabAndAdd(url, false);
        return;
    }
    if (r != QDialog::Accepted)
        return;
    addItem(dlg.result());
}

void DownloadPage::grabAndAdd(const QString &urlText, bool loginSite, int autoHeight)
{
#ifdef HAVE_WEBENGINE
    const QUrl url = QUrl::fromUserInput(urlText);
    QString user, pass, error;
    Release::AudioPending audio;
    const bool audioTrack = Release::peekAudioPending(urlText, &audio);
    if (audioTrack)
        autoHeight = -100;   // звуковая дорожка: только звук, без вопросов
    bool forceLogin = loginSite && !Sites::isLoggedIn(url);
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (loginSite && forceLogin) {
            SiteLoginDialog ld(url, error, window());
            if (ld.exec() != QDialog::Accepted)
                return;
            user = ld.skipLogin() ? QString() : ld.user();
            pass = ld.skipLogin() ? QString() : ld.password();
        }
        GrabDialog gd(url, user, pass, window());
        const int r = gd.exec();
        if (r == QDialog::Accepted) {
            AnalyzeDialog dlg(gd.media().toString(), baseArgs(false), QStringList(), window(), gd.headers());
            dlg.setAutoPick(autoHeight);
            if (dlg.exec() != QDialog::Accepted) {
                if (autoHeight != 0)
                    emit downloadFailed(urlText, T("не удалось определить качество видео"));
                return;
            }
            DownloadPtr item = dlg.result();
            item->external = true;
            item->headers = gd.headers();
            if (!gd.pageTitle().trimmed().isEmpty())
                item->title = safeFileName(gd.pageTitle());
            item->site = Sites::siteName(url);
            item->pageUrl = urlText;
            // Видео релиза из менеджера Adult Empire: файл сразу называется как релиз
            Release::Pending rel;
            if (Release::peekPending(urlText, &rel) && !rel.name.isEmpty()) {
                item->title = safeFileName(rel.name);
                // Качаем во временную папку внутри папки релиза — после переупаковки она удаляется целиком
                if (!rel.dir.isEmpty())
                    item->outDir = QDir(rel.dir).filePath(QStringLiteral(".aristocrat-temp"));
            }
            if (audioTrack) {   // звук другой версии фильма — во временную папку релиза, потом вшивается в MKV
                item->audioOnly = true;
                item->audioTrack = true;
                item->title = safeFileName(audio.base + QStringLiteral(" - ") + audio.title);
                if (!audio.dir.isEmpty())
                    item->outDir = QDir(audio.dir).filePath(QStringLiteral(".aristocrat-temp"));
            }
            if (!gd.thumb().isEmpty())
                item->thumbUrl = gd.thumb();
            addItem(item);
            return;
        }
        if (r != 2) {
            if (autoHeight != 0)
                emit downloadFailed(urlText, T("поиск видео отменён"));
            return;   // отменено
        }
        if (loginSite && (gd.loginFailed() || !forceLogin)) {
            // Вход не удался или старый вход устарел — просим войти ещё раз
            Sites::setLoggedIn(url, false);
            error = gd.loginFailed() ? gd.error()
                                     : T("Видео не найдено. Возможно, вход устарел — войдите в аккаунт ещё раз.");
            forceLogin = true;
            pass.clear();
            continue;
        }
        if (autoHeight != 0) {   // планировщик / звуковая дорожка: без вопросов
            emit downloadFailed(urlText, gd.error());
            return;
        }
        if (Ui::ask(this, T("Загрузка видео"), gd.error() + QStringLiteral("<br><br>") + T("Открыть страницу во встроенном браузере?"),
                    T("Открыть"), T("Отмена")))
            openBrowser(urlText);
        return;
    }
#else
    Q_UNUSED(urlText)
    Q_UNUSED(loginSite)
#endif
}

void DownloadPage::openBrowser(const QString &url)
{
#ifdef HAVE_WEBENGINE
    auto *b = new BrowserWindow(window());
    connect(b, &BrowserWindow::downloadRequested, this,
            [this, b](const QUrl &media, const QStringList &headers, const QString &title, const QString &site,
                      const QString &thumb, const QString &kind) {
        Q_UNUSED(kind)
        AnalyzeDialog dlg(media.toString(), baseArgs(false), QStringList(), b, headers);
        if (dlg.exec() != QDialog::Accepted)
            return;
        DownloadPtr item = dlg.result();
        item->external = true;
        item->headers = headers;
        if (!title.trimmed().isEmpty())
            item->title = safeFileName(title);
        item->site = site;
        if (!thumb.isEmpty())
            item->thumbUrl = thumb;
        addItem(item);
        m_status->setText(T("Видео со страницы добавлено в загрузки: %1").arg(item->title));
    });
    b->show();
    b->open(QUrl::fromUserInput(url.isEmpty() ? QStringLiteral("https://www.google.com") : url));
#else
    Q_UNUSED(url)
    Ui::message(this, T("Браузер"), T("Эта сборка программы собрана без встроенного браузера."));
#endif
}

void DownloadPage::addItem(const DownloadPtr &item)
{
    item->id = m_nextId++;
    if (item->status != DownloadItem::Paused)
        item->status = DownloadItem::Queued;
    m_items << item;
    auto *card = new DownloadCard(item, this);
    m_cards.insert(item->id, card);
    m_cardLayout->insertWidget(m_cardLayout->count() - 1, card);
    connect(card, &DownloadCard::pauseResume, this, &DownloadPage::pauseResume);
    connect(card, &DownloadCard::removeRequested, this, &DownloadPage::removeItem);
    connect(card, &DownloadCard::convertRequested, this, [this](const QString &f) { emit convertRequested(f, false); });
    connect(card, &DownloadCard::chaptersRequested, this, [this](const QString &f, const QString &page) {
        ChaptersDialog d(f, page, window());
        connect(&d, &ChaptersDialog::convertRequested, this, &DownloadPage::chaptersConvert);
        d.exec();
    });
    connect(card, &DownloadCard::releaseRequested, this, &DownloadPage::releaseRequested);
    m_segActive->setChecked(true);
    if (item->thumb.isNull())
        loadThumb(item);
    saveQueue();
    updateView();
    startNext();
}

void DownloadPage::refreshCard(const DownloadPtr &item)
{
    if (DownloadCard *c = m_cards.value(item->id))
        c->refresh();
}

void DownloadPage::startNext()
{
    const int maxTasks = qBound(1, QSettings().value("download/maxTasks", 3).toInt(), 10);
    for (const DownloadPtr &d : m_items) {
        if (m_procs.size() >= maxTasks)
            break;
        if (d->status == DownloadItem::Queued && !m_procs.contains(d->id))
            startItem(d);
    }
    updateView();
}

void DownloadPage::startItem(const DownloadPtr &item)
{
    item->status = DownloadItem::Downloading;
    item->error.clear();
    item->log.clear();
    item->pausing = false;
    QDir().mkpath(itemDir(item));
#ifdef Q_OS_WIN
    // Временная папка внутри папки релиза — скрытая (удаляется, когда релиз готов)
    if (QDir(itemDir(item)).dirName() == QLatin1String(".aristocrat-temp"))
        SetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(itemDir(item)).utf16()), FILE_ATTRIBUTE_HIDDEN);
#endif
    QSettings s;

    QStringList a = baseArgs(!item->noCookies && !item->external);
    if (item->external) {
        // Заголовки, с которыми видео открывал браузер (иначе сайт может не отдать поток)
        for (const QString &h : item->headers)
            a << "--add-header" << h;
    }
    // Вывод (в том числе путь к готовому файлу) — в UTF-8, иначе на Windows русские буквы превращаются в «�»
    a << "--encoding" << "utf-8";
    // Обрывы связи: долгие повторы с растущей паузой, докачка с места обрыва (.part и состояние фрагментов сохраняются)
    a << "--continue" << "--retries" << "30" << "--fragment-retries" << "30"
      << "--retry-sleep" << "exp=2:60" << "--retry-sleep" << "fragment:exp=2:60" << "--socket-timeout" << "30";
    a << "--no-simulate" << "--progress"
      << "--progress-template" << "download:ARISTO|%(progress.downloaded_bytes)s|%(progress.total_bytes)s|%(progress.total_bytes_estimate)s|%(progress.speed)s|%(progress.eta)s"
      << "--print" << "before_dl:ARISTOINFO|%(extractor_key)s|%(duration)s|%(thumbnail)s|%(title)s"
      << "--print" << "after_move:ARISTOFILE|%(filepath)s"
      << "-P" << itemDir(item)
      << "-o" << (item->external && !item->title.isEmpty()
                     ? QString(item->title).replace('%', QStringLiteral("%%")) + QStringLiteral(".%(ext)s")
                     : QStringLiteral("%(title).120B [%(id)s].%(ext)s"));
    if (m_fast->isChecked())
        a << "--concurrent-fragments" << "8";
    if (item->audioTrack) {   // дорожка для MKV релиза — без пережатия, если звук уже AAC
        a << "-f" << "ba/b" << "-x" << "--audio-format" << "m4a";
    } else if (item->audioOnly) {
        a << "-f" << "ba/b" << "-x" << "--audio-format" << "mp3" << "--audio-quality" << "0";
    } else {
        a << "-f" << "bv*+ba/b" << "--merge-output-format" << "mp4/mkv";
        a << "-S" << (item->height > 0 ? QStringLiteral("res:%1,ext:mp4:m4a").arg(item->height) : QStringLiteral("ext:mp4:m4a"));
        if (s.value("download/subs", true).toBool()) {
            const QString lang = s.value("download/subLang", "ru").toString();
            a << "--write-subs" << "--sub-langs" << (lang == QLatin1String("all") ? QStringLiteral("all,-live_chat") : lang + QStringLiteral(".*"));
            if (s.value("download/embedSubs", true).toBool())
                a << "--embed-subs";
        }
    }
    a << item->url;

    auto *proc = new QProcess(this);
    m_procs.insert(item->id, proc);
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, item] { onOutput(item); });
    connect(proc, &QProcess::readyReadStandardError, this, [proc, item] {
        item->log += QString::fromUtf8(proc->readAllStandardError());
        if (item->log.size() > 8000) item->log = item->log.right(8000);
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, item](int code, QProcess::ExitStatus st) { onFinished(item, code, st); });
    connect(proc, &QProcess::errorOccurred, this, [this, item](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            onFinished(item, -1, QProcess::CrashExit);
    });
    proc->setProcessEnvironment(Tools::utf8Env());
    proc->start(Tools::ytdlp(), a);
    refreshCard(item);
}

void DownloadPage::loadThumb(const DownloadPtr &item)
{
    const QString url = item->thumbUrl;
    if (url.isEmpty() || url == QLatin1String("NA"))
        return;
    auto img = QSharedPointer<QImage>::create();
    Ui::runAsync(this, [url, img] { *img = grabImageUrl(url); }, [this, item, img] {
        item->thumb = *img;
        refreshCard(item);
    });
}

void DownloadPage::onOutput(const DownloadPtr &item)
{
    QProcess *proc = m_procs.value(item->id);
    while (proc && proc->canReadLine()) {
        const QString line = QString::fromUtf8(proc->readLine()).trimmed();
        if (line.startsWith(QLatin1String("ARISTO|"))) {
            const QStringList p = line.split('|');
            if (p.size() < 6)
                continue;
            const qint64 done = p[1].toLongLong();
            qint64 total = p[2].toLongLong();
            if (total <= 0)
                total = qint64(p[3].toDouble());
            item->done = done;
            if (total > 0) {
                item->total = total;
                item->progress = qBound(0.0, double(done) / total, 1.0);
            }
            item->speed = p[4].toDouble();
            item->eta = p[5] == QLatin1String("NA") ? -1 : p[5].toInt();
            refreshCard(item);
        } else if (line.startsWith(QLatin1String("ARISTOINFO|"))) {
            const QStringList p = line.mid(11).split('|');
            if (p.size() >= 4) {
                if (item->site.isEmpty() && p[0] != QLatin1String("NA")) item->site = p[0];
                if (item->duration <= 0) item->duration = p[1].toDouble();
                if (item->thumbUrl.isEmpty() && p[2] != QLatin1String("NA")) { item->thumbUrl = p[2]; loadThumb(item); }
                if (item->title.isEmpty() && !item->external) item->title = QStringList(p.mid(3)).join('|');
                refreshCard(item);
                saveQueue();
            }
        } else if (line.startsWith(QLatin1String("ARISTOFILE|"))) {
            item->filePath = line.mid(11);
        }
    }
}

// Готовый файл: путь от yt-dlp; если его нет (кодировка, перемещение) — самый новый файл в папке загрузок
// с тем же названием, иначе просто самый новый видеофайл, появившийся за время загрузки.
QString DownloadPage::locateDownloaded(const DownloadPtr &item) const
{
    if (!item->filePath.isEmpty() && QFileInfo::exists(item->filePath))
        return item->filePath;
    const QDir dir(itemDir(item));
    const QFileInfoList files = dir.entryInfoList({"*.mp4", "*.mkv", "*.webm", "*.mov", "*.m4v", "*.ts", "*.mp3", "*.m4a"},
                                                  QDir::Files, QDir::Time);
    const QString title = item->title.trimmed();
    if (!title.isEmpty())
        for (const QFileInfo &f : files)
            if (f.completeBaseName().startsWith(title, Qt::CaseInsensitive))
                return f.absoluteFilePath();
    for (const QFileInfo &f : files)
        if (f.lastModified().secsTo(QDateTime::currentDateTime()) < 120)
            return f.absoluteFilePath();
    return {};
}

// Ошибка из-за сети (обрыв, нет связи, таймаут, сбой сервера) — стоит повторить
static bool networkError(const QString &log)
{
    static const QRegularExpression re(QStringLiteral(
        "timed out|timeout|connection|getaddrinfo|resolve|unreachable|temporary failure|remote end closed|incompleteread|"
        "reset by peer|broken pipe|ssl|eof occurred|http error 5\\d\\d|giving up after|unable to download (?:video )?(?:data|fragment)|"
        "no route to host|network|errno 1100\\d|winerror 100\\d\\d|10054|10060|10065"), QRegularExpression::CaseInsensitiveOption);
    return re.match(log).hasMatch();
}

// Ссылка на поток больше не принимается (временная ссылка истекла)
bool DownloadPage::linkExpired(const DownloadPtr &item) const
{
    static const QRegularExpression re(QStringLiteral("http error 40[0-4]|http error 410|forbidden|expired|signature"),
                                       QRegularExpression::CaseInsensitiveOption);
    return item->external && !item->pageUrl.isEmpty() && re.match(item->log).hasMatch();
}

bool DownloadPage::retryable(const DownloadPtr &item) const
{
    return networkError(item->log) || linkExpired(item);
}

void DownloadPage::refreshLink(const DownloadPtr &item)
{
#ifdef HAVE_WEBENGINE
    m_status->setText(T("«%1»: беру новую ссылку на видео со страницы…").arg(item->title));
    GrabDialog gd(QUrl::fromUserInput(item->pageUrl), QString(), QString(), window());
    if (gd.exec() == QDialog::Accepted && gd.media().isValid()) {
        item->url = gd.media().toString();
        item->headers = gd.headers();
        item->status = DownloadItem::Queued;   // имя файла прежнее — докачка продолжится с места обрыва
        m_status->setText(T("«%1»: ссылка обновлена, продолжаю загрузку").arg(item->title));
    } else if (item->autoRetries < 40) {
        // Связи всё ещё нет — подождём ещё
        item->status = DownloadItem::Paused;
        item->retryWait = true;
        item->error = T("нет связи — повторю через 60 с");
        QTimer::singleShot(60000, this, [this, item] {
            if (!m_items.contains(item) || !item->retryWait)
                return;
            item->retryWait = false;
            ++item->autoRetries;
            refreshLink(item);
        });
    } else {
        item->status = DownloadItem::Failed;
        item->error = T("не удалось получить новую ссылку на видео — нажмите «Продолжить», когда появится интернет");
    }
#else
    item->status = DownloadItem::Queued;
#endif
    refreshCard(item);
    saveQueue();
    updateView();
    startNext();
}

void DownloadPage::onFinished(const DownloadPtr &item, int code, QProcess::ExitStatus st)
{
    if (!m_procs.contains(item->id))
        return;
    onOutput(item);   // дочитываем остаток вывода
    QProcess *proc = m_procs.take(item->id);
    proc->deleteLater();

    if (item->pausing) {
        item->status = DownloadItem::Paused;
    } else if (st == QProcess::NormalExit && code == 0 && !(item->filePath = locateDownloaded(item)).isEmpty()) {
        item->status = DownloadItem::Done;
        item->autoRetries = 0;
        item->progress = 1;
        item->total = QFileInfo(item->filePath).size();
        if (DownloadCard *c = m_cards.value(item->id)) {
            m_cardLayout->removeWidget(c);
            m_doneLayout->insertWidget(0, c);
        }
        m_segDone->setText(T("Готово (%1)").arg(m_doneLayout->count() - 1));
        emit downloadFinished(item->filePath, item->pageUrl);
        if (m_convertMode->isChecked()) {
            emit convertRequested(item->filePath, true);
            m_status->setText(T("«%1» скачано и отправлено на конвертацию").arg(item->title));
        } else {
            m_status->setText(T("Скачано: %1").arg(item->title));
        }
        if (QSettings().value("general/sound", true).toBool())
            QApplication::beep();
    } else if (!item->noCookies && item->log.contains(QLatin1String("cookie"), Qt::CaseInsensitive)) {
        // Вход из браузера не прочитался — повторяем без него
        item->noCookies = true;
        item->status = DownloadItem::Queued;
        m_status->setText(T("Не удалось взять вход из браузера — качаю без входа"));
    } else if (item->autoRetries < 40 && retryable(item)) {
        // Обрыв связи или протухшая ссылка: ждём и продолжаем с того же места
        ++item->autoRetries;
        const bool expired = linkExpired(item);
        const int delay = qMin(60, 5 * item->autoRetries);
        item->status = DownloadItem::Paused;
        item->retryWait = true;
        item->error = T("связь прервалась — продолжу через %1 с (попытка %2)").arg(delay).arg(item->autoRetries);
        m_status->setText(T("«%1»: %2").arg(item->title, item->error));
        QTimer::singleShot(delay * 1000, this, [this, item, expired] {
            if (!m_items.contains(item) || !item->retryWait || item->status != DownloadItem::Paused)
                return;   // удалено или поставлено на паузу вручную
            item->retryWait = false;
            item->error.clear();
            item->log.clear();
            // Протухшая ссылка (или каждая 3-я попытка у видео со страницы) — берём новую со страницы
            if (item->external && !item->pageUrl.isEmpty() && (expired || item->autoRetries % 3 == 0)) {
                refreshLink(item);
                return;
            }
            item->status = DownloadItem::Queued;
            refreshCard(item);
            saveQueue();
            startNext();
        });
    } else {
        item->status = DownloadItem::Failed;
        if (!item->pageUrl.isEmpty())
            QTimer::singleShot(0, this, [this, page = item->pageUrl] { emit downloadFailed(page, T("загрузка видео не удалась")); });
        QStringList lines;
        for (const QString &l : item->log.split('\n', Qt::SkipEmptyParts))
            if (l.startsWith(QLatin1String("ERROR")))
                lines << l.mid(l.indexOf(':') + 1).trimmed();
        QString err = lines.isEmpty() ? (code == -1 ? T("модуль загрузки не запустился") : T("загрузка прервана")) : lines.last();
        if (err.contains(QLatin1String("DRM"), Qt::CaseInsensitive))
            err = T("видео защищено DRM — такие видео программа не скачивает");
        item->error = err;
    }
    refreshCard(item);
    saveQueue();
    updateView();
    startNext();
}

void DownloadPage::pauseResume(const DownloadPtr &item)
{
    if (QProcess *proc = m_procs.value(item->id)) {
        item->pausing = true;
        proc->kill();     // недокачанная часть (.part) сохраняется, yt-dlp продолжит с того же места
        return;
    }
    if (item->retryWait) {   // ждал автоповтора — пользователь продолжил сам
        item->retryWait = false;
        item->error.clear();
        item->autoRetries = 0;
        item->status = DownloadItem::Queued;
    } else if (item->status == DownloadItem::Paused || item->status == DownloadItem::Failed) {
        item->status = DownloadItem::Queued;
        item->autoRetries = 0;
    }
    else if (item->status == DownloadItem::Queued)
        item->status = DownloadItem::Paused;
    refreshCard(item);
    saveQueue();
    startNext();
}

QString DownloadPage::pageStatus(const QString &pageUrl) const
{
    auto norm = [](const QString &u) {
        QUrl x(u.trimmed());
        x.setQuery(QString());
        x.setFragment(QString());
        QString h = x.host();
        if (h.startsWith(QLatin1String("www.")))
            h = h.mid(4);
        return h + x.path();
    };
    const QString want = norm(pageUrl);
    for (const DownloadPtr &d : m_items) {
        if (d->audioTrack || norm(d->pageUrl) != want)
            continue;
        switch (d->status) {
        case DownloadItem::Queued: return T("в очереди загрузок");
        case DownloadItem::Downloading: return T("качается %1%").arg(int(d->progress * 100));
        case DownloadItem::Paused: return d->retryWait ? T("ждёт связи (%1%)").arg(int(d->progress * 100)) : T("загрузка на паузе");
        case DownloadItem::Done: return QString();
        case DownloadItem::Failed: return T("загрузка не удалась");
        }
    }
    return {};
}

void DownloadPage::showStatus(const QString &text)
{
    m_status->setText(text);
}

void DownloadPage::removeLeftovers(const DownloadPtr &item)
{
    const QString title = item->title.trimmed();
    if (title.isEmpty())
        return;
    QDir dir(itemDir(item));
    for (const QFileInfo &f : dir.entryInfoList({title + QStringLiteral("*.part"), title + QStringLiteral("*.part-Frag*"),
                                                 title + QStringLiteral("*.ytdl"), title + QStringLiteral("*.temp.*")}, QDir::Files))
        QFile::remove(f.absoluteFilePath());
    if (!item->outDir.isEmpty() && dir.dirName() == QLatin1String(".aristocrat-temp") && dir.isEmpty())
        dir.removeRecursively();
}

void DownloadPage::removeItem(const DownloadPtr &item)
{
    if (QProcess *proc = m_procs.value(item->id)) {
        item->pausing = true;
        proc->kill();
        proc->waitForFinished(3000);
    }
    if (item->status != DownloadItem::Done)
        removeLeftovers(item);
    m_items.removeAll(item);
    if (DownloadCard *c = m_cards.take(item->id))
        c->deleteLater();
    saveQueue();
    updateView();
}

static QString queueFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/downloads.json");
}

void DownloadPage::saveQueue() const
{
    QJsonArray arr;
    for (const DownloadPtr &d : m_items) {
        if (d->status == DownloadItem::Done)
            continue;
        QJsonObject o;
        o["url"] = d->url; o["title"] = d->title; o["site"] = d->site; o["duration"] = d->duration;
        o["thumb"] = d->thumbUrl; o["height"] = d->height; o["audio"] = d->audioOnly; o["audioTrack"] = d->audioTrack; o["noCookies"] = d->noCookies;
        o["progress"] = d->progress;
        o["external"] = d->external;
        o["page"] = d->pageUrl;
        o["outDir"] = d->outDir;
        o["headers"] = QJsonArray::fromStringList(d->headers);
        arr << o;
    }
    QFile f(queueFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson());
}

void DownloadPage::loadQueue()
{
    QFile f(queueFile());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    f.close();
    const bool resume = QSettings().value("download/resume", true).toBool();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        auto d = DownloadPtr::create();
        d->url = o["url"].toString(); d->title = o["title"].toString(); d->site = o["site"].toString();
        d->duration = o["duration"].toDouble(); d->thumbUrl = o["thumb"].toString(); d->height = o["height"].toInt();
        d->audioOnly = o["audio"].toBool(); d->audioTrack = o["audioTrack"].toBool(); d->noCookies = o["noCookies"].toBool(); d->progress = o["progress"].toDouble();
        d->external = o["external"].toBool();
        d->pageUrl = o["page"].toString();
        d->outDir = o["outDir"].toString();
        for (const QJsonValue &h : o["headers"].toArray()) d->headers << h.toString();
        d->status = resume ? DownloadItem::Queued : DownloadItem::Paused;
        if (!d->url.isEmpty())
            addItem(d);
    }
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
    m_pauseAll->setEnabled(!m_procs.isEmpty());
}

static QString safeFileName(QString t)
{
    static const QRegularExpression bad(QStringLiteral("[\\\\/:*?\"<>|\\r\\n\\t]+"));
    t.replace(bad, QStringLiteral(" "));
    t = t.simplified().left(150);
    while (t.endsWith('.')) t.chop(1);
    return t.isEmpty() ? QStringLiteral("video") : t;
}

void DownloadPage::addExternal(const QJsonObject &o)
{
    auto item = DownloadPtr::create();
    item->external = true;
    item->url = o.value("url").toString();
    item->title = safeFileName(o.value("title").toString());
    item->height = o.value("height").toInt();
    item->duration = o.value("duration").toDouble();
    item->thumbUrl = o.value("thumbnail").toString();
    const QString page = o.value("pageUrl").toString();
    item->pageUrl = page;
    item->site = QUrl(page).host();
    if (item->site.isEmpty())
        item->site = QUrl(item->url).host();
    const QJsonObject h = o.value("headers").toObject();
    bool hasReferer = false;
    for (auto it = h.begin(); it != h.end(); ++it) {
        const QString v = it.value().toString();
        if (v.isEmpty() || it.key().contains(':') || v.contains('\n'))
            continue;
        item->headers << it.key() + QStringLiteral(":") + v;
        if (it.key().compare(QLatin1String("referer"), Qt::CaseInsensitive) == 0)
            hasReferer = true;
    }
    if (!hasReferer && !page.isEmpty())
        item->headers << QStringLiteral("Referer:") + page;
    m_status->setText(T("Из браузера получено: %1").arg(item->title));
    addItem(item);
}

void DownloadPage::showExtensionHelp()
{
    // Распаковываем расширение из программы в постоянную папку (её и указываем браузеру)
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/browser-extension");
    const QStringList files = {"background.js", "popup.html", "popup.css", "popup.js",
                               "icons/icon16.png", "icons/icon32.png", "icons/icon48.png", "icons/icon128.png"};
    const QString chromeDir = base + QStringLiteral("/Chrome-Yandex-Edge-Opera");
    const QString ffDir = base + QStringLiteral("/Firefox");
    for (const QString &dir : {chromeDir, ffDir}) {
        QDir().mkpath(dir + QStringLiteral("/icons"));
        for (const QString &f : files) {
            QFile::remove(dir + '/' + f);
            QFile::copy(QStringLiteral(":/ext/") + f, dir + '/' + f);
            QFile(dir + '/' + f).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadOther);
        }
        QFile::remove(dir + QStringLiteral("/manifest.json"));
        QFile::copy(dir == ffDir ? QStringLiteral(":/ext/manifest.firefox.json") : QStringLiteral(":/ext/manifest.json"),
                    dir + QStringLiteral("/manifest.json"));
        QFile(dir + QStringLiteral("/manifest.json")).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadOther);
    }

    FramedDialog d(this);
    d.setWindowTitle(T("Расширение для браузера"));
    d.setMinimumWidth(680);
    auto *lay = d.contentLayout();
    lay->setContentsMargins(26, 20, 26, 20);
    lay->setSpacing(12);
    auto *text = new QLabel(T(
        "<b>Aristocrat — захват видео</b> находит видео на открытой странице (как Download Helper) и одним нажатием "
        "передаёт его в программу. Так скачиваются сайты, которые не поддерживаются напрямую.<br><br>"
        "<b>Яндекс Браузер, Chrome, Edge, Opera:</b><br>"
        "1. Откройте адрес <code>browser://extensions</code> (в Chrome — <code>chrome://extensions</code>).<br>"
        "2. Включите «Режим разработчика».<br>"
        "3. Нажмите «Загрузить распакованное расширение» и выберите папку <b>Chrome-Yandex-Edge-Opera</b>.<br><br>"
        "<b>Firefox:</b> откройте <code>about:debugging#/runtime/this-firefox</code> → «Загрузить временное дополнение» → "
        "файл manifest.json в папке <b>Firefox</b>. В обычном Firefox такое дополнение работает до перезапуска браузера.<br><br>"
        "Как пользоваться: откройте страницу с видео, запустите воспроизведение, нажмите на значок Aristocrat "
        "в панели браузера и выберите «Скачать». Программа при этом должна быть запущена."), &d);
    text->setWordWrap(true);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(text);
    auto *btns = new QHBoxLayout;
    auto *open = Ui::primaryButton(T("Открыть папку с расширением"), &d);
    auto *ok = new QPushButton(T("Закрыть"), &d);
    btns->addWidget(open);
    btns->addStretch();
    btns->addWidget(ok);
    lay->addLayout(btns);
    connect(open, &QPushButton::clicked, &d, [base] { QDesktopServices::openUrl(QUrl::fromLocalFile(base)); });
    connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
    d.exec();
}
