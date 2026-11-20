#pragma once
#include <QString>
#include <QStringList>

// Настройки программы (хранятся через QSettings).
namespace Settings {

enum AfterDone { DoNothing = 0, CloseApp = 1, Sleep = 2, Shutdown = 3 };

QString outputDir();
void setOutputDir(const QString &dir);
QStringList recentOutputDirs();

QString globalPresetId();
void setGlobalPresetId(const QString &id);

QStringList recentPresets();
void setRecentPresets(const QStringList &ids);

bool hardwareAccel();
void setHardwareAccel(bool on);

bool mergeAll();
void setMergeAll(bool on);

int afterDone();
void setAfterDone(int action);

QString lastOpenDir();
void setLastOpenDir(const QString &dir);

} // namespace Settings
