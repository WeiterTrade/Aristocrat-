#include "core/I18n.h"
#include "core/Theme.h"
#include "core/Tools.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSplashScreen>
#include <QSvgRenderer>
#include <QTimer>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QThread>
#include <QThreadPool>

// Заставка при запуске: тёмная карточка, логотип, название и версия (как у Aimersoft UniConverter)
static QSplashScreen *makeSplash()
{
    const qreal dpr = qApp->primaryScreen() ? qApp->primaryScreen()->devicePixelRatio() : 1.0;
    const QSize size(540, 300);
    QPixmap pm(size * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    QPainterPath card;
    card.addRoundedRect(QRectF(0.5, 0.5, size.width() - 1, size.height() - 1), 16, 16);
    QLinearGradient bg(0, 0, 0, size.height());
    bg.setColorAt(0, QColor(0x2b, 0x26, 0x23));
    bg.setColorAt(1, QColor(0x1b, 0x18, 0x16));
    p.fillPath(card, bg);
    p.setPen(QPen(QColor(212, 166, 74, 90), 1));
    p.drawPath(card);

    QSvgRenderer logo(QStringLiteral(":/logo/logo.svg"));
    logo.render(&p, QRectF(70, 92, 96, 96));

    const QColor gold(0xd4, 0xa6, 0x4a), muted(0x9a, 0x8f, 0x84);
    QFont f = qApp->font();
    f.setPointSizeF(26);
    f.setBold(true);
    f.setLetterSpacing(QFont::PercentageSpacing, 104);
    p.setFont(f);
    p.setPen(gold);
    p.drawText(QRectF(190, 96, 330, 44), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("ARISTOCRAT"));
    f.setPointSizeF(11.5);
    f.setBold(false);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 3.2);
    p.setFont(f);
    p.setPen(QColor(0xe8, 0xdf, 0xd4));
    p.drawText(QRectF(192, 140, 330, 24), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("ВИДЕОКОНВЕРТЕР"));
    f.setPointSizeF(10);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    p.setFont(f);
    p.setPen(muted);
    p.drawText(QRectF(192, 168, 330, 22), Qt::AlignLeft | Qt::AlignVCenter,
               T("Версия: %1").arg(QStringLiteral(APP_VERSION)));
    f.setPointSizeF(9);
    p.setFont(f);
    p.drawText(QRectF(0, size.height() - 44, size.width(), 24), Qt::AlignCenter, QStringLiteral("Aristocrat TEAM"));
    p.end();

    auto *s = new QSplashScreen(pm, Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::SplashScreen);
    s->setAttribute(Qt::WA_TranslucentBackground);
    s->setAttribute(Qt::WA_DeleteOnClose);
    return s;
}

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);   // нужно встроенному браузеру
#ifdef Q_OS_LINUX
    // В AppImage песочница Chromium часто недоступна — отключаем, чтобы встроенный браузер запускался
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_DISABLE_SANDBOX"))
        qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");
#endif
    QApplication app(argc, argv);
    // После перезапуска из настроек ждём, пока предыдущая копия закроется
    if (app.arguments().contains(QStringLiteral("--restarted")))
        QThread::msleep(1500);
    QApplication::setOrganizationName(QStringLiteral("Aristocrat"));
    QApplication::setApplicationName(QStringLiteral("Aristocrat Видеоконвертер"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/logo/logo.svg")));
    QApplication::setStyle(QStringLiteral("Fusion"));
    I18n::init();   // язык: из настроек, иначе как в системе, иначе английский
    Theme::init();  // тема оформления  // одинаковый вид на Windows и Linux

    QFile qss(QStringLiteral(":/style.qss"));
    if (qss.open(QIODevice::ReadOnly))
        app.setStyleSheet(Theme::map(QString::fromUtf8(qss.readAll())));

    // Проверка аппаратных кодировщиков заранее, в фоне — чтобы не задерживать первую конвертацию.
    QThreadPool::globalInstance()->start([] {
        Tools::hardwareH264Encoder();
        Tools::hardwareHevcEncoder();
    });

    if (app.arguments().contains(QStringLiteral("--demo-splash"))) {   // снимок заставки для автопроверки
        QSplashScreen *only = makeSplash();
        only->show();
        return app.exec();
    }

    // Заставка (не в автопроверке сборки — там служебные ключи --demo-… и т. п.)
    bool service = false;
    for (const QString &a : app.arguments().mid(1))
        service |= a.startsWith(QLatin1String("--"));
    QSplashScreen *splash = service ? nullptr : makeSplash();
    QElapsedTimer shown;
    if (splash) {
        splash->show();
        shown.start();
        app.processEvents();
    }

    MainWindow w;   // окно готовится, пока видна заставка
    if (splash) {
        // Как в оригинале: сначала заставка (не меньше 1,5 с), она закрывается — и только потом появляется программа
        QTimer::singleShot(qMax<qint64>(0, 1500 - shown.elapsed()), splash, [splash, &w] {
            splash->close();
            w.show();
            w.raise();
            w.activateWindow();
        });
    } else {
        w.show();
    }

    // Файлы, переданные при запуске («Открыть с помощью…»).
    // Служебные ключи для автопроверки сборки: --page=N (открыть раздел), --demo-trim (окно обрезки).
    QStringList files;
    for (const QString &a : app.arguments().mid(1)) {
        if (a.startsWith(QLatin1String("--page=")))
            w.showPage(a.mid(7).toInt());
        else if (a == QLatin1String("--demo-trim"))
            w.demoTrim();
        else if (a == QLatin1String("--restarted"))
            continue;
        else if (a.startsWith(QLatin1String("--demo-dvd=")))
            w.demoDvd(a.mid(11));
        else if (a.startsWith(QLatin1String("--demo-chapters=")))
            w.demoChapters(a.mid(16).section(QLatin1Char('|'), 0, 0), a.mid(16).section(QLatin1Char('|'), 1));
        else if (a.startsWith(QLatin1String("--demo-release=")))
            w.demoRelease(a.mid(15).section(QLatin1Char('|'), 0, 0), a.mid(15).section(QLatin1Char('|'), 1, 1),
                          a.mid(15).section(QLatin1Char('|'), 2, 2));
        else if (a == QLatin1String("--demo-server"))
            w.demoServer();
        else if (a == QLatin1String("--demo-upload"))
            w.demoUpload();
        else if (a.startsWith(QLatin1String("--demo-theme=")))
            w.demoTheme(a.mid(13));
        else if (a.startsWith(QLatin1String("--demo-grab=")))
            w.demoGrab(a.mid(12));
        else if (a.startsWith(QLatin1String("--demo-browser=")))
            w.demoBrowser(a.mid(15));
        else if (a.startsWith(QLatin1String("--demo-settings")))
            w.demoSettings(a.section('=', 1).toInt());
        else
            files << a;
    }
    if (!files.isEmpty())
        w.openFiles(files);
    return app.exec();
}
