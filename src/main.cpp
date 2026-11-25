#include "core/I18n.h"
#include "core/Theme.h"
#include "core/Tools.h"
#include "core/DvdCss.h"
#include "core/DvdImage.h"
#include <cstdio>
#include "ui/MainWindow.h"

#include <QApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSplashScreen>
#include <QSvgRenderer>
#include <QTimer>
#include <QLocalSocket>
#include <QLocalServer>
#include <QFileInfo>
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

    // Автопроверка правки меню DVD: --dvd-menu-test=<образ>|<новый образ>|<картинка>
    for (const QString &a : app.arguments())
        if (a.startsWith(QLatin1String("--dvd-menu-test="))) {
            const QStringList p = a.mid(16).split(QLatin1Char('|'));
            QString err;
            const QList<DvdImage::Menu> ms = DvdImage::menus(p.value(0), &err);
            for (const auto &m : ms)
                fprintf(stderr, "dvdmenu: %s cells=%d still=%d buttons=%d\n", qPrintable(m.title), int(m.cells.size()), int(m.still),
                        int(m.buttons.size()));
            if (ms.isEmpty()) {
                fprintf(stderr, "dvdmenu: none %s\n", qPrintable(err));
                return 1;
            }
            const QSize ds = DvdImage::videoInfo(p.value(0), ms.first()).display();
            const QImage before = DvdImage::frame(p.value(0), ms.first(), &err);
            QImage pic(p.value(2));
            pic = pic.scaled(ds);
            const bool ok = DvdImage::save(p.value(0), p.value(1), {{ms.first(), pic}}, {}, &err);
            const QImage after = DvdImage::frame(p.value(1), DvdImage::menus(p.value(1), nullptr).value(0), &err);
            const QRgb b = before.isNull() ? 0 : before.pixel(before.width() / 2, before.height() / 2);
            const QRgb c = after.isNull() ? 0 : after.pixel(after.width() / 2, after.height() / 2);
            fprintf(stderr, "dvdmenu: display=%dx%d save=%d before=%d,%d,%d after=%d,%d,%d err=%s\n", ds.width(), ds.height(), ok, qRed(b),
                    qGreen(b), qBlue(b), qRed(c), qGreen(c), qBlue(c), qPrintable(err));
            return ok && qGreen(c) > 150 && qRed(c) < 100 ? 0 : 1;
        }

    // Регион привода (Linux: запускается через pkexec из окна «Копирование DVD»): --set-dvd-region=<привод>|<регион>
    for (const QString &a : app.arguments())
        if (a.startsWith(QLatin1String("--set-dvd-region="))) {
            const QStringList p = a.mid(17).split(QLatin1Char('|'));
            QString err;
            const bool ok = DvdCss::setDriveRegion(p.value(0), p.value(1).toInt(), &err);
            if (!ok)
                fprintf(stderr, "%s\n", qPrintable(err));
            return ok ? 0 : 1;
        }

    // Автопроверка копирования DVD через libdvdcss: --dvd-css-test=<образ или привод>|<папка>
    for (const QString &a : app.arguments())
        if (a.startsWith(QLatin1String("--dvd-css-test="))) {
            const QStringList p = a.mid(15).split(QLatin1Char('|'));
            QString err;
            int bad = 0;
            const bool ok = DvdCss::available() && DvdCss::backup(p.value(0), {QStringLiteral("01")}, p.value(1), {}, &err, &bad);
            fprintf(stderr, "dvdcss: lib=%s ok=%d bad=%d err=%s\n", qPrintable(DvdCss::libraryPath()), ok, bad, qPrintable(err));
            return ok ? 0 : 1;
        }

    // Автопроверка: оглавление DVD по IFO и чтение основного фильма: --dvd-rip-test=<образ>|<папка VIDEO_TS>|<файл>
    for (const QString &a : app.arguments())
        if (a.startsWith(QLatin1String("--dvd-rip-test="))) {
            const QStringList p = a.mid(15).split(QLatin1Char('|'));
            QString err;
            QList<DvdCss::DiscTitle> ts = DvdCss::discTitles(p.value(0), p.value(1), &err);
            const DvdCss::Cells traps = DvdCss::probeTraps(p.value(0), ts, {});
            for (const auto &t : ts)
                fprintf(stderr, "dvdtitle: %d vts=%d sec=%.1f cells=%d jumps=%d bytes=%lld main=%d audio=%s\n", t.number, t.vts, t.seconds,
                        int(t.cells.size()), t.jumps, (long long)t.bytes, int(t.main), qPrintable(t.audioFormat.join(QLatin1Char(','))));
            bool ok = false;
            int bad = 0;
            for (const auto &t : ts)
                if (t.main)
                    ok = DvdCss::rip(p.value(0), t, p.value(2), 0, {}, &err, &bad);
            fprintf(stderr, "dvdrip: titles=%d ok=%d bad=%d err=%s\n", int(ts.size()), ok, bad, qPrintable(err));
            if (p.size() > 3) {   // и полная копия в ISO
                const bool iok = DvdCss::isoBackup(p.value(0), p.value(3), 0, ts, traps, {}, &err, &bad);
                fprintf(stderr, "dvdiso: ok=%d bad=%d err=%s\n", iok, bad, qPrintable(err));
                ok = ok && iok;
            }
            return ok ? 0 : 1;
        }

    if (app.arguments().contains(QStringLiteral("--demo-splash"))) {   // снимок заставки для автопроверки
        QSplashScreen *only = makeSplash();
        only->show();
        return app.exec();
    }

    // Одна копия программы: если уже запущена (например, свёрнута в трей) — показать её и передать файлы
    const bool autostart = app.arguments().contains(QStringLiteral("--autostart"));
    bool testRun = false;
    for (const QString &a : app.arguments().mid(1))
        testRun |= a.startsWith(QLatin1String("--demo")) || a.startsWith(QLatin1String("--dvd-")) || a.startsWith(QLatin1String("--set-dvd"));
    const QString serverName = QStringLiteral("aristocrat-videoconverter-") + qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER"));
    QLocalServer instance;
    if (!testRun) {
        // После «Перезапустить» старая копия ещё закрывается — подождать её
        for (int i = 0; i < (app.arguments().contains(QStringLiteral("--restarted")) ? 20 : 1); ++i) {
            QLocalSocket probe;
            probe.connectToServer(serverName);
            if (!probe.waitForConnected(300))
                break;
            if (i + 1 < (app.arguments().contains(QStringLiteral("--restarted")) ? 20 : 1)) {
                probe.disconnectFromServer();
                QThread::msleep(500);
                continue;
            }
            if (!autostart) {   // автозапуск при уже открытой программе ничего не делает
                QStringList files;
                for (const QString &a : app.arguments().mid(1))
                    if (!a.startsWith(QLatin1String("--")))
                        files << QFileInfo(a).absoluteFilePath();
                probe.write((QStringLiteral("show\n") + files.join(QLatin1Char('\n'))).toUtf8());
                probe.waitForBytesWritten(1000);
            }
            return 0;
        }
        QLocalServer::removeServer(serverName);
        instance.listen(serverName);
    }
    // Свёрнутая в трей программа закрывается только через «Выход»
    QApplication::setQuitOnLastWindowClosed(false);

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

    Tools::cleanAppDir();   // хвосты прошлых загрузок из папки программы
    MainWindow w;   // окно готовится, пока видна заставка
    if (splash) {
        // Как в оригинале: сначала заставка (не меньше 1,5 с), она закрывается — и только потом появляется программа
        QTimer::singleShot(qMax<qint64>(0, 1500 - shown.elapsed()), splash, [splash, &w] {
            splash->close();
            w.show();
            w.raise();
            w.activateWindow();
        });
    } else if (autostart) {
        QTimer::singleShot(0, &w, [&w] { w.startInBackground(); });
    } else {
        w.show();
    }
    // Вторая копия просит показаться и открыть файлы
    QObject::connect(&instance, &QLocalServer::newConnection, &w, [&instance, &w] {
        QLocalSocket *c = instance.nextPendingConnection();
        QObject::connect(c, &QLocalSocket::readyRead, &w, [c, &w] {
            const QStringList lines = QString::fromUtf8(c->readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            w.showNormal();
            w.raise();
            w.activateWindow();
            if (lines.size() > 1)
                w.openFiles(lines.mid(1));
        });
        QObject::connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
    });

    // Файлы, переданные при запуске («Открыть с помощью…»).
    // Служебные ключи для автопроверки сборки: --page=N (открыть раздел), --demo-trim (окно обрезки).
    QStringList files;
    for (const QString &a : app.arguments().mid(1)) {
        if (a.startsWith(QLatin1String("--page=")))
            w.showPage(a.mid(7).toInt());
        else if (a == QLatin1String("--demo-trim"))
            w.demoTrim();
        else if (a == QLatin1String("--restarted") || a == QLatin1String("--autostart"))
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
        else if (a.startsWith(QLatin1String("--demo-menu=")))
            w.demoMenu(a.mid(12));
        else if (a == QLatin1String("--demo-scheduler"))
            w.demoScheduler();
        else if (a.startsWith(QLatin1String("--demo-covers")))
            w.demoCovers(a.section(QLatin1Char('='), 1));
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
