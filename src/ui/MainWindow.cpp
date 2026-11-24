#include "MainWindow.h"
#include "DiscStudio.h"
#include <QSharedPointer>
#include <QPushButton>
#include "core/MediaInfo.h"
#include "CoverArt.h"
#ifdef HAVE_WEBENGINE
#include "ReleaseScheduler.h"
#include "AudioTracks.h"
#endif
#include "core/Theme.h"
#include "ConvertPage.h"
#include "DownloadPage.h"
#include "Frameless.h"
#include "SettingsDialog.h"
#include "core/LocalServer.h"
#ifdef HAVE_WEBENGINE
#include "PageGrabber.h"
#endif
#include "core/I18n.h"
#include "core/Theme.h"
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include "PlaceholderPage.h"
#include "ToolboxPage.h"
#include "ChaptersDialog.h"
#include "core/Release.h"
#include <QDir>
#include "Widgets.h"

#include <QApplication>
#include <QDialog>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
#include <QGuiApplication>
#include <QScreen>
#include <QStyle>
#include <QTimer>
#include <QAbstractButton>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Aristocrat Видеоконвертер"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint);
    setWindowIcon(QIcon(QStringLiteral(":/logo/logo.svg")));
    setMinimumSize(960, 600);
    {
        const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
        resize(qMin(1480, avail.width() - 20), qMin(880, avail.height() - 20));
    }

    // Своя рамка: внешний виджет с отступом (за него тянем размер), внутри — всё остальное
    auto *frame = new QWidget(this);
    frame->setObjectName(QStringLiteral("MainFrame"));
    frame->setAttribute(Qt::WA_StyledBackground);
    m_frameLayout = new QVBoxLayout(frame);
    m_frameLayout->setContentsMargins(4, 4, 4, 4);
    auto *central = new QWidget(frame);
    m_frameLayout->addWidget(central);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    QWidget *header = buildHeader();
    root->addWidget(header);
    new DragArea(this, header, true);

    m_pages = new QStackedWidget(central);
    m_convert = new ConvertPage(m_pages);
    auto *toolbox = new ToolboxPage(m_pages);
    m_toolbox = toolbox;
    m_pages->addWidget(m_convert);
    auto *download = new DownloadPage(m_pages);
    m_download = download;
    m_pages->addWidget(download);
    {
        // «Запись»: инструменты для дисков (выдвижная панель разделов)
        auto *studio = new DiscStudioPage(m_pages);
        m_menuEditor = new MenuEditor(studio);
        studio->addTool(QStringLiteral("dvd"), T("Редактировать ISO (DVD)"), m_menuEditor);
        m_pages->addWidget(studio);
    }
    m_pages->addWidget(new PlaceholderPage("transfer", T("Передача на устройство"),
                                           T("Передача видео на телефон появится в одной из следующих версий. "
                                                          "Пока выберите формат нужного устройства во вкладке «Устройства»."), m_pages));
    m_pages->addWidget(toolbox);
    root->addWidget(m_pages, 1);
    setCentralWidget(frame);
    new FrameResizer(this, frame, 4);

    connect(m_nav, &QButtonGroup::idClicked, m_pages, &QStackedWidget::setCurrentIndex);
    connect(toolbox, &ToolboxPage::dvdRequested, this, [this](const QList<QPair<QString, QString>> &titles, int mode, const QString &tempDir) {
        m_convert->addDvdTitles(titles, mode, tempDir);
        showPage(0);
    });
    connect(toolbox, &ToolboxPage::releaseVideo, this,
            [this](const QString &in, const QString &name, const QString &dir, bool lossless, const QString &meta) {
                m_convert->addReleaseVideo(in, name, dir, lossless, meta);
            });
    // Менеджер Adult Empire: видео качается в разделе «Загрузить», затем переупаковывается в папку релиза
    connect(toolbox, &ToolboxPage::releaseDownload, this, [this, download](const QString &url) {
        showPage(1);
        download->grabAndAdd(url, true);
    });
#ifdef HAVE_WEBENGINE
    // Планировщик релизов: видео качается в 720p без вопросов, готовность и ошибки — ему же
    {
        auto *sched = ReleaseScheduler::instance();
        sched->requestDownload = [download](const QString &page) { download->grabAndAdd(page, true, 720); };
        sched->downloadStatus = [download](const QString &page) { return download->pageStatus(page); };
        sched->convertStatus = [this](const QString &dir) { return m_convert->releaseStatus(dir); };
        connect(m_convert, &ConvertPage::releaseVideoDone, sched, &ReleaseScheduler::onVideoDone);
        connect(download, &DownloadPage::downloadFailed, sched, &ReleaseScheduler::onDownloadFailed);
        connect(sched, &ReleaseScheduler::batchFinished, this, [this](const QList<SchedItem> &done) { schedulerDone(done); });
    }
#endif
#ifdef HAVE_WEBENGINE
    // Звук другой версии фильма скачан — вшить в MKV релиза
    connect(AudioMuxer::instance(), &AudioMuxer::message, download, &DownloadPage::showStatus);
    connect(download, &DownloadPage::downloadFinished, this, [](const QString &file, const QString &page) {
        Release::AudioPending a;
        if (Release::takeAudioPending(page, &a))
            AudioMuxer::instance()->add(file, a);
    });
#endif
    connect(download, &DownloadPage::downloadFinished, this, [this](const QString &file, const QString &page) {
        Release::Pending p;
        if (page.isEmpty() || !Release::takePending(page, &p))
            return;
        const QString fanart = QDir(p.dir).filePath(p.name + QStringLiteral("-fanart.jpg"));
        if (!QFileInfo::exists(fanart))
            Release::fanartFromVideo(file, fanart);
#ifdef HAVE_WEBENGINE
        // Скачалось в 4K, а «4K Релиз» не был включён — жанр в NFO и значок на постер
        if (!p.k4) {
            auto height = QSharedPointer<int>::create(0);
            Ui::runAsync(this, [file, height] { *height = MediaInfo::probe(file).height; }, [this, p, height] {
                if (*height < 2000)
                    return;
                const QDir d(p.dir);
                CoverArt::addGenre(d.filePath(p.name + QStringLiteral(".nfo")), QStringLiteral("4K Ultra HD"));
                const QString poster = d.filePath(p.name + QStringLiteral("-poster.jpg"));
                if (QFileInfo::exists(poster))
                    CoverArt::stampPoster(poster, true, false, this, [](bool) {});
            });
        }
#endif
        m_convert->addReleaseVideo(file, p.name, p.dir, p.lossless, p.chaptersMeta, true);   // скачанное — удалить после обработки
    });
    connect(download, &DownloadPage::chaptersConvert, this,
            [this](const QString &in, const QString &name, const QString &dir, const QString &meta) {
                m_convert->addReleaseVideo(in, name, dir, false, meta);
                showPage(0);
            });
    connect(download, &DownloadPage::releaseRequested, this, [toolbox](const QString &file, const QString &page) {
        toolbox->openRelease(page, file);
    });
    connect(toolbox, &ToolboxPage::restoreRequested, this, [this](const QStringList &files, UpscaleOptions::Mode mode, bool vhs) {
        m_convert->addFilesWithUpscale(files, mode, vhs);
        m_nav->button(0)->setChecked(true);
        m_pages->setCurrentIndex(0);
    });
    connect(download, &DownloadPage::convertRequested, this, [this](const QString &file, bool autoStart) {
        m_convert->addFileAndConvert(file, autoStart);
        if (!autoStart) {
            m_nav->button(0)->setChecked(true);
            m_pages->setCurrentIndex(0);
        }
    });

    // Связь с расширением браузера
    auto *server = new LocalServer(this);
    server->start();
    connect(server, &LocalServer::mediaReceived, this, [this](const QJsonObject &media) {
        m_download->addExternal(media);
        showPage(1);
        if (isMinimized())
            showNormal();
        raise();
        activateWindow();
    });

    QSettings s;
    restoreGeometry(s.value("window/geometry").toByteArray());
}

void MainWindow::openFiles(const QStringList &files)
{
    m_convert->addFiles(files);
}

void MainWindow::showPage(int index)
{
    if (QAbstractButton *b = m_nav->button(index)) {
        b->setChecked(true);
        m_pages->setCurrentIndex(index);
    }
}

void MainWindow::demoGrab(const QString &url)
{
#ifdef HAVE_WEBENGINE
    // Автопроверка: вход, подтверждение 18+ и поиск видео без окон (логин/пароль — из переменных окружения)
    QSettings().setValue(QStringLiteral("sites/adultConfirmed"), true);
    auto *g = new PageGrabber(this);
    connect(g, &PageGrabber::progress, this, [](const QString &t) { qInfo("GRAB STEP %s", qPrintable(t)); });
    connect(g, &PageGrabber::finished, this, [](const QUrl &m, const QStringList &h, const QString &title, const QString &) {
        qInfo("GRAB OK %s | %s | %s", qPrintable(m.toString()), qPrintable(title), qPrintable(h.filter(QStringLiteral("Cookie")).join(' ')));
    });
    connect(g, &PageGrabber::failed, this, [](const QString &r, bool login) { qInfo("GRAB FAIL %s login=%d", qPrintable(r), login); });
    g->start(QUrl(url), qEnvironmentVariable("ARISTOCRAT_TEST_USER"), qEnvironmentVariable("ARISTOCRAT_TEST_PASS"));
#else
    Q_UNUSED(url)
#endif
}

void MainWindow::demoBrowser(const QString &url)
{
    showPage(1);
    QTimer::singleShot(1500, this, [this, url] { m_download->openBrowser(url); });
}

void MainWindow::demoSettings(int tab)
{
    QTimer::singleShot(2500, this, [this, tab] { openSettings(tab); });
}

void MainWindow::openSettings(int tab)
{
    SettingsDialog dlg(this, tab);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_convert->reloadSettings();
    m_download->reloadSettings();
    if (dlg.themeChanged())
        applyTheme();
    if (dlg.languageChanged()
        && Ui::ask(this, T("Настройки"), T("Язык интерфейса сменится после перезапуска. Перезапустить программу сейчас?"),
                   T("Перезапустить"), T("Позже")))
        restartApp();
}

void MainWindow::demoDvd(const QString &path)
{
    showPage(4);
    QTimer::singleShot(1500, this, [this, path] { m_toolbox->openDvd(path); });
}

void MainWindow::demoChapters(const QString &file, const QString &url)
{
    // Автопроверка редактора глав: файл + ссылка → сцены загружаются сами
    showPage(4);
    QTimer::singleShot(1000, this, [this, file, url] {
        auto *d = new ChaptersDialog(file, url, this);
        connect(d, &ChaptersDialog::convertRequested, this,
                [this](const QString &in, const QString &name, const QString &dir, const QString &meta) {
                    m_convert->addReleaseVideo(in, name, dir, false, meta);
                });
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->show();
    });
}

void MainWindow::demoRelease(const QString &url, const QString &video, const QString &scenes)
{
    showPage(4);
    QTimer::singleShot(1000, this, [this, url, video, scenes] { m_toolbox->openRelease(url, video, scenes); });
}

void MainWindow::demoUpload()
{
    showPage(4);
    QTimer::singleShot(1000, this, [this] { m_toolbox->openRelease(QString(), QString(), QString(), 1); });
}

#ifdef HAVE_WEBENGINE
// Очередь планировщика пройдена: сигнал и список релизов с актрисами — остаётся заполнить анкеты
void MainWindow::schedulerDone(const QList<SchedItem> &done)
{
    DoneSound::play();
    if (isMinimized())
        showNormal();
    raise();
    activateWindow();
    auto *d = new FramedDialog(this, true);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle(T("Планировщик: готово"));
    d->resize(720, 420);
    auto *lay = d->contentLayout();
    lay->setContentsMargins(16, 12, 16, 12);
    lay->addWidget(new QLabel(T("Релизы готовы и лежат в библиотеке. Осталось заполнить анкеты актрис и нажать «Записать на сервер», "
                                "затем «Изображения релиза → сервер»."), d));
    for (const SchedItem &it : done) {
        auto *row = new QHBoxLayout;
        QString text = QStringLiteral("<b>%1</b>").arg((it.title.isEmpty() ? it.url : it.title).toHtmlEscaped());
        if (it.state == SchedItem::Done) {
            const QStringList a = ReleaseScheduler::actressesOf(it.dest);
            text += QStringLiteral("<br>") + (a.isEmpty() ? T("актрис в NFO нет") : T("Актрисы: %1").arg(a.join(QStringLiteral(", ")).toHtmlEscaped()));
        } else {
            text += QStringLiteral("<br><span style='color:#E07A6A'>%1</span>").arg(ReleaseScheduler::stateName(it).toHtmlEscaped());
        }
        auto *l = new QLabel(text, d);
        l->setWordWrap(true);
        row->addWidget(l, 1);
        if (it.state == SchedItem::Done) {
            auto *b = Ui::primaryButton(T("Заполнить анкеты"), d);
            const QString dest = it.dest;
            connect(b, &QPushButton::clicked, this, [this, dest] {
                QSettings().setValue(QStringLiteral("ae/lastRelease"), dest);
                showPage(4);
                m_toolbox->openRelease(QString(), QString(), QString(), 2);
            });
            row->addWidget(b);
        }
        lay->addLayout(row);
    }
    lay->addStretch();
    auto *close = new QPushButton(T("Закрыть"), d);
    connect(close, &QPushButton::clicked, d, &QDialog::close);
    auto *br = new QHBoxLayout;
    br->addStretch();
    br->addWidget(close);
    lay->addLayout(br);
    d->show();
}
#endif

void MainWindow::demoScheduler()
{
    showPage(4);
#ifdef HAVE_WEBENGINE
    if (ReleaseScheduler::instance()->items().isEmpty()) {   // пример для снимка
        ReleaseScheduler::instance()->add(QStringLiteral("https://www.adultempire.com/5151061/my-friends-hot-mom-vol-104-porn-videos.html"));
        ReleaseScheduler::instance()->add(QStringLiteral("https://www.adultempire.com/5070099/all-ass-no-brakes-porn-movies.html"));
    }
#endif
    QTimer::singleShot(1000, this, [this] { m_toolbox->openRelease(QString(), QString(), QString(), 4); });
}

void MainWindow::demoCovers(const QString &stampFile)
{
    showPage(4);
    QTimer::singleShot(1000, this, [this] { m_toolbox->openRelease(QString(), QString(), QString(), 3); });
#ifdef HAVE_WEBENGINE
    if (!stampFile.isEmpty())   // проверка значков: 4K + DVD функциями генератора
        CoverArt::stampPoster(stampFile, true, true, this, [](bool ok) { qInfo("STAMP %s", ok ? "ok" : "failed"); });
#endif
}

void MainWindow::demoServer()
{
    showPage(4);
    QTimer::singleShot(1000, this, [this] { m_toolbox->openRelease(QString(), QString(), QString(), 2); });
}

void MainWindow::demoTheme(const QString &theme)
{
    // Автопроверка смены темы на лету
    QTimer::singleShot(4000, this, [this, theme] {
        qputenv("ARISTOCRAT_THEME", theme.toUtf8());
        applyTheme();
    });
}

void MainWindow::applyTheme()
{
    // Смена темы на лету: новые цвета стилей, значков и картинок без перезапуска
    Theme::init();
    QFile qss(QStringLiteral(":/style.qss"));
    if (qss.open(QIODevice::ReadOnly))
        qApp->setStyleSheet(Theme::map(QString::fromUtf8(qss.readAll())));
    Ui::refreshTheme();
}

void MainWindow::restartApp()
{
    if (m_convert->isBusy() || m_download->isBusy()) {
        if (!Ui::ask(this, T("Перезапуск"), T("Идёт конвертация или загрузка. Прервать её и перезапустить программу?"),
                     T("Перезапустить"), T("Отмена")))
            return;
    }
    QString exe = QCoreApplication::applicationFilePath();
#ifdef Q_OS_LINUX
    if (!qEnvironmentVariableIsEmpty("APPIMAGE"))
        exe = qEnvironmentVariable("APPIMAGE");   // AppImage перезапускаем целиком
#endif
    // Новая копия подождёт, пока эта закроется (браузер не любит два экземпляра на одном профиле)
    if (!QProcess::startDetached(exe, {QStringLiteral("--restarted")}, QFileInfo(exe).absolutePath())) {
        Ui::message(this, T("Перезапуск"), T("Не удалось перезапустить программу. Закройте и откройте её вручную."));
        return;
    }
    m_forceClose = true;
    QTimer::singleShot(0, qApp, [] { qApp->exit(0); });
}

void MainWindow::demoTrim()
{
    QTimer::singleShot(7000, m_convert, &ConvertPage::openFirstTrim);
}

QWidget *MainWindow::buildHeader()
{
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("Header"));
    header->setAttribute(Qt::WA_StyledBackground);
    header->setFixedHeight(112);
    auto *lay = new QHBoxLayout(header);
    lay->setContentsMargins(18, 0, 0, 0);

    // Логотип и название
    auto *brand = new QHBoxLayout;
    brand->setSpacing(10);
    auto *logo = new QLabel(header);
    logo->setPixmap(QIcon(QStringLiteral(":/logo/logo.svg")).pixmap(44, 44));
    auto *names = new QVBoxLayout;
    names->setSpacing(0);
    auto *t1 = new QLabel(QStringLiteral("ARISTOCRAT"), header);
    t1->setObjectName(QStringLiteral("AppTitle"));
    auto *t2 = new QLabel(QStringLiteral("ВИДЕОКОНВЕРТЕР"), header);
    t2->setObjectName(QStringLiteral("AppSubtitle"));
    names->addStretch();
    names->addWidget(t1);
    names->addWidget(t2);
    names->addStretch();
    brand->addWidget(logo);
    brand->addLayout(names);
    auto *brandW = new QWidget(header);
    brandW->setObjectName(QStringLiteral("Brand"));
    brandW->setLayout(brand);
    brandW->setFixedWidth(260);
    brandW->setContentsMargins(0, 10, 0, 0);
    lay->addWidget(brandW, 0, Qt::AlignTop | Qt::AlignLeft);
    lay->addStretch();

    // Навигация — как в привычной программе: Конвертировать, Загрузить, Запись, Передать, Инструменты
    m_nav = new QButtonGroup(this);
    m_nav->setExclusive(true);
    struct Nav { const char *icon; QString text; };
    const QList<Nav> items = {{"convert", T("Конвертировать")}, {"download", T("Загрузить")},
                              {"burn", T("Запись")}, {"transfer", T("Передать")},
                              {"toolbox", T("Инструменты")}};
    for (int i = 0; i < items.size(); ++i) {
        auto *b = new QToolButton(header);
        b->setObjectName(QStringLiteral("NavButton"));
        b->setText(items[i].text);
        b->setIcon(Ui::navIcon(items[i].icon));
        b->setIconSize(QSize(34, 34));
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setMinimumHeight(100);
        m_nav->addButton(b, i);
        lay->addWidget(b, 0, Qt::AlignBottom);
    }
    m_nav->button(0)->setChecked(true);
    lay->addStretch();

    // Меню (☰)
    auto *menuBtn = Ui::iconButton("menu", T("Меню"), header, 22);
    menuBtn->setPopupMode(QToolButton::InstantPopup);
    menuBtn->setStyleSheet(QStringLiteral("QToolButton::menu-indicator{image:none;}"));
    auto *menu = new QMenu(menuBtn);
    menu->addAction(T("Настройки"), this, [this] { openSettings(); });
    menu->addSeparator();
    menu->addAction(T("О программе"), this, [this] {
        Ui::message(this, T("О программе"),
                    Theme::map(T("<h3 style='color:#E8C266'>Aristocrat Видеоконвертер</h3>"
                                   "Версия %1<br><br>Конвертер видео и аудио с восстановлением и апскейлом старых записей.<br><br>"
                                   "Использует FFmpeg (LGPL/GPL), Real-ESRGAN (BSD-3) и yt-dlp (Unlicense).")
                        .arg(QStringLiteral(APP_VERSION))));
    });
    menu->addAction(T("Выход"), this, &QWidget::close);
    menuBtn->setMenu(menu);
    auto *menuW = new QWidget(header);
    menuW->setObjectName(QStringLiteral("MenuArea"));
    auto *ml = new QHBoxLayout(menuW);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->setSpacing(6);
    ml->addStretch();
    ml->addWidget(menuBtn, 0, Qt::AlignTop);
    ml->addWidget(new WindowButtons(this, true, true, menuW), 0, Qt::AlignTop);
    menuW->setFixedWidth(260);
    lay->addWidget(menuW, 0, Qt::AlignTop | Qt::AlignRight);
    lay->setContentsMargins(18, 0, 0, 0);
    return header;
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_forceClose) {
        e->accept();
        return;
    }
    if (m_convert->isBusy() || m_download->isBusy()) {
        if (!Ui::ask(this, T("Идёт работа"),
                     T("Конвертация или загрузка ещё не закончена. Закрыть программу и прервать её?"),
                     T("Закрыть"), T("Отмена"))) {
            e->ignore();
            return;
        }
    }
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
    // Отдельные окна (менеджер Adult Empire) закрываются вместе с программой
    for (QWidget *w : QApplication::topLevelWidgets())
        if (w != this && w->isVisible() && !w->parentWidget() && qobject_cast<QDialog *>(w))
            w->close();
    e->accept();
}

void MainWindow::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::WindowStateChange && m_frameLayout) {
        const bool maxed = isMaximized() || isFullScreen();
        m_frameLayout->setContentsMargins(maxed ? 0 : 4, maxed ? 0 : 4, maxed ? 0 : 4, maxed ? 0 : 4);
        centralWidget()->setProperty("maximized", maxed);
        centralWidget()->style()->unpolish(centralWidget());
        centralWidget()->style()->polish(centralWidget());
    }
    QMainWindow::changeEvent(e);
}
