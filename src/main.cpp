#include "core/I18n.h"
#include "core/Theme.h"
#include "core/Tools.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QThread>
#include <QThreadPool>

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

    MainWindow w;
    w.show();

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
