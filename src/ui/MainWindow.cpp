#include "MainWindow.h"
#include "ConvertPage.h"
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
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Aristocrat Video Converter"));
    setWindowIcon(QIcon(QStringLiteral(":/logo/logo.svg")));
    setMinimumSize(1100, 680);
    resize(1480, 880);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildHeader());

    m_pages = new QStackedWidget(central);
    m_convert = new ConvertPage(m_pages);
    auto *toolbox = new ToolboxPage(m_pages);
    m_pages->addWidget(m_convert);
    m_pages->addWidget(new PlaceholderPage("download", QStringLiteral("Загрузка видео"),
                                           QStringLiteral("Скачивание роликов по ссылке появится в одной из следующих версий."), m_pages));
    m_pages->addWidget(new PlaceholderPage("burn", QStringLiteral("Запись DVD"),
                                           QStringLiteral("Запись видео на DVD появится в одной из следующих версий."), m_pages));
    m_pages->addWidget(new PlaceholderPage("transfer", QStringLiteral("Передача на устройство"),
                                           QStringLiteral("Передача видео на телефон появится в одной из следующих версий. "
                                                          "Пока выберите формат нужного устройства во вкладке «Устройства»."), m_pages));
    m_pages->addWidget(toolbox);
    root->addWidget(m_pages, 1);
    setCentralWidget(central);

    connect(m_nav, &QButtonGroup::idClicked, m_pages, &QStackedWidget::setCurrentIndex);
    connect(toolbox, &ToolboxPage::restoreRequested, this, [this](const QStringList &files, UpscaleOptions::Mode mode, bool vhs) {
        m_convert->addFilesWithUpscale(files, mode, vhs);
        m_nav->button(0)->setChecked(true);
        m_pages->setCurrentIndex(0);
    });

    QSettings s;
    restoreGeometry(s.value("window/geometry").toByteArray());
}

void MainWindow::openFiles(const QStringList &files)
{
    m_convert->addFiles(files);
}

QWidget *MainWindow::buildHeader()
{
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("Header"));
    header->setAttribute(Qt::WA_StyledBackground);
    header->setFixedHeight(112);
    auto *lay = new QHBoxLayout(header);
    lay->setContentsMargins(18, 8, 18, 0);

    // Логотип и название
    auto *brand = new QHBoxLayout;
    brand->setSpacing(10);
    auto *logo = new QLabel(header);
    logo->setPixmap(QIcon(QStringLiteral(":/logo/logo.svg")).pixmap(44, 44));
    auto *names = new QVBoxLayout;
    names->setSpacing(0);
    auto *t1 = new QLabel(QStringLiteral("ARISTOCRAT"), header);
    t1->setObjectName(QStringLiteral("AppTitle"));
    auto *t2 = new QLabel(QStringLiteral("VIDEO CONVERTER"), header);
    t2->setObjectName(QStringLiteral("AppSubtitle"));
    names->addStretch();
    names->addWidget(t1);
    names->addWidget(t2);
    names->addStretch();
    brand->addWidget(logo);
    brand->addLayout(names);
    auto *brandW = new QWidget(header);
    brandW->setLayout(brand);
    brandW->setFixedWidth(260);
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
    menu->addAction(QStringLiteral("О программе"), this, [this] {
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("О программе"));
        box.setIconPixmap(QIcon(QStringLiteral(":/logo/logo.svg")).pixmap(96, 96));
        box.setText(QStringLiteral("<h3 style='color:#E8C266'>Aristocrat Video Converter</h3>"
                                   "Версия %1<br><br>Конвертер видео и аудио с восстановлением и апскейлом старых записей.<br><br>"
                                   "Использует FFmpeg (LGPL/GPL) и Real-ESRGAN (BSD-3).")
                        .arg(QStringLiteral(APP_VERSION)));
        box.exec();
    });
    menu->addAction(QStringLiteral("Выход"), this, &QWidget::close);
    menuBtn->setMenu(menu);
    auto *menuW = new QWidget(header);
    auto *ml = new QHBoxLayout(menuW);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->addStretch();
    ml->addWidget(menuBtn);
    menuW->setFixedWidth(260);
    lay->addWidget(menuW, 0, Qt::AlignTop | Qt::AlignRight);
    return header;
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_convert->isBusy()) {
        const auto r = QMessageBox::question(this, QStringLiteral("Идёт конвертация"),
                                             QStringLiteral("Конвертация ещё не закончена. Закрыть программу и прервать её?"));
        if (r != QMessageBox::Yes) {
            e->ignore();
            return;
        }
    }
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
    e->accept();
}
