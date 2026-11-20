#include "Tools.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace Tools {

static QString exeName(const QString &name)
{
#ifdef Q_OS_WIN
    return name + QStringLiteral(".exe");
#else
    return name;
#endif
}

QString findExecutable(const QString &name, const QStringList &subdirs)
{
    const QString app = QCoreApplication::applicationDirPath();
    QStringList dirs;
    for (const QString &s : subdirs)
        dirs << app + "/tools/" + s << app + "/" + s;
    dirs << app + "/tools" << app << app + "/../share/aristocrat-video-converter/tools";

    for (const QString &d : dirs) {
        QFileInfo fi(QDir(d).filePath(exeName(name)));
        if (fi.exists() && fi.isExecutable())
            return fi.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(name);
}

QString ffmpeg()
{
    static QString p = findExecutable(QStringLiteral("ffmpeg"), {QStringLiteral("ffmpeg"), QStringLiteral("ffmpeg/bin")});
    return p;
}

QString ffprobe()
{
    static QString p = findExecutable(QStringLiteral("ffprobe"), {QStringLiteral("ffmpeg"), QStringLiteral("ffmpeg/bin")});
    return p;
}

QString realesrgan()
{
    static QString p = findExecutable(QStringLiteral("realesrgan-ncnn-vulkan"), {QStringLiteral("realesrgan")});
    return p;
}

QString realesrganModelsDir()
{
    const QString exe = realesrgan();
    if (exe.isEmpty())
        return {};
    return QFileInfo(exe).absolutePath() + QStringLiteral("/models");
}

bool hasFfmpeg() { return !ffmpeg().isEmpty() && !ffprobe().isEmpty(); }
bool hasRealesrgan() { return !realesrgan().isEmpty(); }

static bool testEncoder(const QString &encoder)
{
    if (ffmpeg().isEmpty())
        return false;
    QProcess p;
    p.start(ffmpeg(), {"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                       "color=black:s=256x256:d=0.2", "-frames:v", "2", "-c:v", encoder,
                       "-f", "null", "-"});
    if (!p.waitForFinished(15000)) {
        p.kill();
        return false;
    }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

QString hardwareH264Encoder()
{
    static bool checked = false;
    static QString found;
    if (!checked) {
        checked = true;
        for (const QString &e : {QStringLiteral("h264_nvenc"), QStringLiteral("h264_qsv"), QStringLiteral("h264_amf")}) {
            if (testEncoder(e)) { found = e; break; }
        }
    }
    return found;
}

QString hardwareHevcEncoder()
{
    static bool checked = false;
    static QString found;
    if (!checked) {
        checked = true;
        for (const QString &e : {QStringLiteral("hevc_nvenc"), QStringLiteral("hevc_qsv"), QStringLiteral("hevc_amf")}) {
            if (testEncoder(e)) { found = e; break; }
        }
    }
    return found;
}

QString escapeFilterPath(const QString &path)
{
    QString p = QDir::fromNativeSeparators(path);
    p.replace(QStringLiteral("\\"), QStringLiteral("/"));
    p.replace(QStringLiteral(":"), QStringLiteral("\\:"));
    p.replace(QStringLiteral("'"), QStringLiteral("\\'"));
    return QStringLiteral("'") + p + QStringLiteral("'");
}

} // namespace Tools
