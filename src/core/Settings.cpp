#include "Settings.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace Settings {

static QSettings &s()
{
    static QSettings settings;
    return settings;
}

QString outputDir()
{
    QString def = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (def.isEmpty())
        def = QDir::homePath();
    def += QStringLiteral("/Aristocrat Видеоконвертер");
    return s().value("outputDir", def).toString();
}

void setOutputDir(const QString &dir)
{
    s().setValue("outputDir", dir);
    QStringList r = recentOutputDirs();
    r.removeAll(dir);
    r.prepend(dir);
    while (r.size() > 6)
        r.removeLast();
    s().setValue("recentOutputDirs", r);
}

QStringList recentOutputDirs() { return s().value("recentOutputDirs").toStringList(); }

QString globalPresetId() { return s().value("globalPreset", "mp4-source").toString(); }
void setGlobalPresetId(const QString &id) { s().setValue("globalPreset", id); }

QStringList recentPresets() { return s().value("recentPresets").toStringList(); }
void setRecentPresets(const QStringList &ids) { s().setValue("recentPresets", ids); }

bool hardwareAccel() { return s().value("hardwareAccel", false).toBool(); }
void setHardwareAccel(bool on) { s().setValue("hardwareAccel", on); }

bool mergeAll() { return s().value("mergeAll", false).toBool(); }
void setMergeAll(bool on) { s().setValue("mergeAll", on); }

int afterDone() { return s().value("afterDone", 0).toInt(); }
void setAfterDone(int action) { s().setValue("afterDone", action); }

QString lastOpenDir() { return s().value("lastOpenDir", QDir::homePath()).toString(); }
void setLastOpenDir(const QString &dir) { s().setValue("lastOpenDir", dir); }

} // namespace Settings
