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
    QApplication::setApplicationName(QStringLiteral("Aristocrat Video Converter"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/logo/logo.svg")));
    QApplication::setStyle(QStringLiteral("Fusion"));  // одинаковый вид на Windows и Linux

    QFile qss(QStringLiteral(":/style.qss"));
    if (qss.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    // Проверка аппаратных кодировщиков заранее, в фоне — чтобы не задерживать первую конвертацию.
    QThreadPool::globalInstance()->start([] {
        Tools::hardwareH264Encoder();
        Tools::hardwareHevcEncoder();
    });

    MainWindow w;
    w.show();

    // Файлы, переданные при запуске («Открыть с помощью…»)
    const QStringList files = app.arguments().mid(1);
    if (!files.isEmpty())
        w.openFiles(files);
    return app.exec();
}
