#include "core/I18n.h"
#include "core/Theme.h"
#include "core/Tools.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QThreadPool>

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
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
        else if (a.startsWith(QLatin1String("--demo-settings")))
            w.demoSettings(a.section('=', 1).toInt());
        else
            files << a;
    }
    if (!files.isEmpty())
        w.openFiles(files);
    return app.exec();
}
