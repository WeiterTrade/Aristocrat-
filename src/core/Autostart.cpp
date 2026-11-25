#include "Autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace Autostart {

static QString command()
{
#ifdef Q_OS_WIN
    return QStringLiteral("\"%1\" --autostart").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
#else
    const QString exe = qEnvironmentVariableIsEmpty("APPIMAGE") ? QCoreApplication::applicationFilePath() : qEnvironmentVariable("APPIMAGE");
    return QStringLiteral("\"%1\" --autostart").arg(exe);
#endif
}

#ifndef Q_OS_WIN
static QString desktopFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/autostart/aristocrat-videoconverter.desktop");
}
#endif

void set(bool on)
{
#ifdef Q_OS_WIN
    QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
    if (on)
        run.setValue(QStringLiteral("Aristocrat Videoconverter"), command());
    else
        run.remove(QStringLiteral("Aristocrat Videoconverter"));
#else
    if (!on) {
        QFile::remove(desktopFile());
        return;
    }
    QDir().mkpath(QFileInfo(desktopFile()).absolutePath());
    QFile f(desktopFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=Aristocrat Видеоконвертер\n"
                               "Comment=Продолжение незаконченного апскейла\nExec=%1\nX-GNOME-Autostart-enabled=true\nTerminal=false\n")
                    .arg(command())
                    .toUtf8());
#endif
}

bool isSet()
{
#ifdef Q_OS_WIN
    return QSettings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat)
        .contains(QStringLiteral("Aristocrat Videoconverter"));
#else
    return QFile::exists(desktopFile());
#endif
}

bool enabledByUser() { return QSettings().value(QStringLiteral("general/resumeAfterReboot"), true).toBool(); }
void setEnabledByUser(bool on) { QSettings().setValue(QStringLiteral("general/resumeAfterReboot"), on); }

} // namespace Autostart
