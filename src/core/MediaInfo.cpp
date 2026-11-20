#include "MediaInfo.h"
#include "Tools.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QtMath>

QString StreamTrack::label() const
{
    if (isDefault)
        return QStringLiteral("По умолчанию");
    QStringList parts;
    parts << QStringLiteral("Дорожка %1").arg(index + 1);
    if (!language.isEmpty() && language != QLatin1String("und"))
        parts << language;
    else if (!title.isEmpty())
        parts << title;
    return parts.join(QStringLiteral(" · "));
}

QString StreamTrack::details() const
{
    QStringList parts;
    parts << QStringLiteral("#%1").arg(index + 1);
    if (!language.isEmpty() && language != QLatin1String("und"))
        parts << language;
    if (!codec.isEmpty())
        parts << codec.toUpper();
    if (channels > 0)
        parts << (channels == 1 ? QStringLiteral("моно") : channels == 2 ? QStringLiteral("стерео") : QStringLiteral("%1 кан.").arg(channels));
    if (!title.isEmpty())
        parts << title;
    return parts.join(QStringLiteral(" · "));
}

static double parseRate(const QString &r)
{
    const QStringList p = r.split('/');
    if (p.size() == 2) {
        const double d = p[1].toDouble();
        return d > 0 ? p[0].toDouble() / d : 0.0;
    }
    return r.toDouble();
}

MediaInfo MediaInfo::probe(const QString &path)
{
    MediaInfo mi;
    mi.path = path;
    QFileInfo fi(path);
    mi.size = fi.size();
    mi.container = fi.suffix().toUpper();

    if (Tools::ffprobe().isEmpty()) {
        mi.error = QStringLiteral("Не найден ffprobe. Положите FFmpeg в папку tools рядом с программой.");
        return mi;
    }

    QProcess p;
    p.start(Tools::ffprobe(), {"-v", "error", "-print_format", "json", "-show_format", "-show_streams", path});
    if (!p.waitForFinished(30000)) {
        p.kill();
        mi.error = QStringLiteral("ffprobe не ответил");
        return mi;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(p.readAllStandardOutput());
    if (!doc.isObject()) {
        mi.error = QString::fromUtf8(p.readAllStandardError()).trimmed();
        if (mi.error.isEmpty())
            mi.error = QStringLiteral("Файл не распознан");
        return mi;
    }

    const QJsonObject root = doc.object();
    const QJsonObject fmt = root.value("format").toObject();
    mi.duration = fmt.value("duration").toString().toDouble();
    mi.bitrate = fmt.value("bit_rate").toString().toLongLong();

    int audioIdx = 0, subIdx = 0;
    for (const QJsonValue &v : root.value("streams").toArray()) {
        const QJsonObject s = v.toObject();
        const QString type = s.value("codec_type").toString();
        const QJsonObject tags = s.value("tags").toObject();
        if (type == QLatin1String("video") && !mi.hasVideo) {
            // Пропускаем обложки (attached_pic)
            if (s.value("disposition").toObject().value("attached_pic").toInt() == 1)
                continue;
            mi.hasVideo = true;
            mi.width = s.value("width").toInt();
            mi.height = s.value("height").toInt();
            mi.videoCodec = s.value("codec_name").toString();
            mi.fps = parseRate(s.value("avg_frame_rate").toString());
            if (mi.fps <= 0 || mi.fps > 240)
                mi.fps = parseRate(s.value("r_frame_rate").toString());
            const QString fo = s.value("field_order").toString();
            mi.interlaced = !fo.isEmpty() && fo != QLatin1String("progressive") && fo != QLatin1String("unknown");
            const double sar = parseRate(s.value("sample_aspect_ratio").toString());
            if (sar > 0.1)
                mi.sar = sar;
            mi.rotation = tags.value("rotate").toString().toInt();
            if (mi.duration <= 0)
                mi.duration = s.value("duration").toString().toDouble();
        } else if (type == QLatin1String("audio")) {
            StreamTrack t;
            t.index = audioIdx++;
            t.codec = s.value("codec_name").toString();
            t.channels = s.value("channels").toInt();
            t.language = tags.value("language").toString();
            t.title = tags.value("title").toString();
            t.isDefault = s.value("disposition").toObject().value("default").toInt() == 1;
            mi.audioTracks << t;
        } else if (type == QLatin1String("subtitle")) {
            StreamTrack t;
            t.index = subIdx++;
            t.codec = s.value("codec_name").toString();
            t.language = tags.value("language").toString();
            t.title = tags.value("title").toString();
            mi.subtitleTracks << t;
        }
    }

    // Ровно одна дорожка «по умолчанию»: отмеченная в файле, иначе первая
    bool haveDefault = false;
    for (StreamTrack &t : mi.audioTracks) {
        if (t.isDefault && !haveDefault) haveDefault = true;
        else t.isDefault = false;
    }
    if (!haveDefault && !mi.audioTracks.isEmpty())
        mi.audioTracks.first().isDefault = true;

    mi.ok = mi.hasVideo || !mi.audioTracks.isEmpty();
    if (!mi.ok)
        mi.error = QStringLiteral("В файле нет видео или звука");
    return mi;
}
