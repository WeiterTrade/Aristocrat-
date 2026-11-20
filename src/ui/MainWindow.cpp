#include "MainWindow.h"
#include "ConvertPage.h"
#include "DownloadPage.h"
#include "Frameless.h"
#include "SettingsDialog.h"
#include "core/I18n.h"
#include <QProcess>
#include "PlaceholderPage.h"
#include "ToolboxPage.h"
#include "Widgets.h"

#include <QButtonGroup>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
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
    setMinimumSize(1100, 680);
    resize(1480, 880);

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
    m_pages->addWidget(m_convert);
    auto *download = new DownloadPage(m_pages);
    m_download = download;
    m_pages->addWidget(download);
    m_pages->addWidget(new PlaceholderPage("burn", QStringLiteral("Запись DVD"),
                                           QStringLiteral("Запись видео на DVD появится в одной из следующих версий."), m_pages));
    m_pages->addWidget(new PlaceholderPage("transfer", QStringLiteral("Передача на устройство"),
                                           QStringLiteral("Передача видео на телефон появится в одной из следующих версий. "
                                                          "Пока выберите формат нужного устройства во вкладке «Устройства»."), m_pages));
    m_pages->addWidget(toolbox);
    root->addWidget(m_pages, 1);
    setCentralWidget(frame);
    new FrameResizer(this, frame, 4);

    connect(m_nav, &QButtonGroup::idClicked, m_pages, &QStackedWidget::setCurrentIndex);
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
    if (dlg.languageChanged()
        && Ui::ask(this, T("Язык"), T("Язык интерфейса сменится после перезапуска. Перезапустить программу сейчас?"),
                   T("Перезапустить"), T("Позже"))) {
        if (!m_convert->isBusy() && !m_download->isBusy()) {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
            qApp->quit();
        }
    }
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
    const QList<Nav> items = {{"convert", QStringLiteral("Конвертировать")}, {"download", QStringLiteral("Загрузить")},
                              {"burn", QStringLiteral("Запись")}, {"transfer", QStringLiteral("Передать")},
                              {"toolbox", QStringLiteral("Инструменты")}};
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
    auto *menuBtn = Ui::iconButton("menu", QStringLiteral("Меню"), header, 22);
    menuBtn->setPopupMode(QToolButton::InstantPopup);
    menuBtn->setStyleSheet(QStringLiteral("QToolButton::menu-indicator{image:none;}"));
    auto *menu = new QMenu(menuBtn);
    menu->addAction(QStringLiteral("Настройки"), this, [this] { openSettings(); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("О программе"), this, [this] {
        Ui::message(this, QStringLiteral("О программе"),
                    QStringLiteral("<h3 style='color:#E8C266'>Aristocrat Видеоконвертер</h3>"
                                   "Версия %1<br><br>Конвертер видео и аудио с восстановлением и апскейлом старых записей.<br><br>"
                                   "Использует FFmpeg (LGPL/GPL), Real-ESRGAN (BSD-3) и yt-dlp (Unlicense).")
                        .arg(QStringLiteral(APP_VERSION)));
    });
    menu->addAction(QStringLiteral("Выход"), this, &QWidget::close);
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
    if (m_convert->isBusy() || m_download->isBusy()) {
        if (!Ui::ask(this, QStringLiteral("Идёт работа"),
                     QStringLiteral("Конвертация или загрузка ещё не закончена. Закрыть программу и прервать её?"),
                     QStringLiteral("Закрыть"), QStringLiteral("Отмена"))) {
            e->ignore();
            return;
        }
    }
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
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
