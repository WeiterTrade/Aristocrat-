#include "Task.h"

#include <QtMath>

bool EditOptions::isEmpty() const
{
    return trimStart <= 0 && trimEnd <= 0 && !hasVideoFilters() && audioTrack == 0 && volume == 100;
}

bool EditOptions::hasVideoFilters() const
{
    return !crop.isNull() || rotate != 0 || flipH || flipV || brightness != 0 || contrast != 0
        || saturation != 0 || deinterlace || denoise || !watermarkText.isEmpty()
        || !watermarkImage.isEmpty() || !subtitleFile.isEmpty() || subtitleTrack >= 0;
}

QString UpscaleOptions::modeName(Mode m)
{
    switch (m) {
    case None: return QStringLiteral("Без апскейла");
    case Fast1080: return QStringLiteral("1080p — быстро");
    case Fast4K: return QStringLiteral("4K — быстро");
    case AI1080: return QStringLiteral("1080p — нейросеть");
    case AI4K: return QStringLiteral("4K — нейросеть");
    }
    return {};
}

double ConversionTask::effectiveDuration() const
{
    double end = edit.trimEnd > 0 ? qMin(edit.trimEnd, info.duration) : info.duration;
    return qMax(0.0, end - edit.trimStart);
}

QSize ConversionTask::outputSize() const
{
    if (preset.isAudioOnly() || !info.hasVideo)
        return {};
    if (upscale.mode != UpscaleOptions::None)
        return QSize(upscale.targetWidth(), upscale.targetHeight());
    if (customResolution.isValid() && !customResolution.isEmpty())
        return customResolution;
    if (preset.width > 0 && preset.height > 0)
        return QSize(preset.width, preset.height);

    QSize s(info.width, info.height);
    if (!edit.crop.isNull())
        s = edit.crop.size();
    if (edit.rotate == 90 || edit.rotate == 270)
        s.transpose();
    return s;
}

int ConversionTask::videoBitrateKbps() const
{
    if (customVBitrate > 0)
        return customVBitrate;
    if (upscale.mode == UpscaleOptions::Fast4K || upscale.mode == UpscaleOptions::AI4K)
        return qMax(preset.vbitrate, 20000);
    if (upscale.mode == UpscaleOptions::Fast1080 || upscale.mode == UpscaleOptions::AI1080)
        return qMax(preset.vbitrate, 8000);
    return preset.vbitrate;
}

qint64 ConversionTask::estimatedBytes() const
{
    const double dur = effectiveDuration();
    if (dur <= 0)
        return 0;
    double kbps = 0;
    if (!preset.isAudioOnly() && info.hasVideo) {
        int v = videoBitrateKbps();
        if (v <= 0) {
            // Оцениваем по исходнику с поправкой на площадь кадра
            const double srcKbps = info.bitrate > 0 ? info.bitrate / 1000.0 : 3000.0;
            const QSize o = outputSize();
            const double area = (info.width > 0 && info.height > 0 && o.isValid())
                                    ? double(o.width()) * o.height() / (double(info.width) * info.height)
                                    : 1.0;
            v = int(srcKbps * qBound(0.25, area, 16.0) * 1.1);
        }
        kbps += v;
    }
    if (edit.audioTrack >= 0 && !info.audioTracks.isEmpty()) {
        if (preset.abitrate > 0)
            kbps += preset.abitrate;
        else if (preset.acodec == QLatin1String("pcm_s16le"))
            kbps += 1411;
        else if (preset.acodec == QLatin1String("flac"))
            kbps += 800;
        else
            kbps += 192;
    }
    return qint64(kbps * 1000.0 / 8.0 * dur);
}

QString ConversionTask::outputFileName() const
{
    return outputName + QLatin1Char('.') + preset.ext;
}

QString formatDuration(double seconds)
{
    const qint64 s = qint64(qRound64(seconds));
    return QStringLiteral("%1:%2:%3")
        .arg(s / 3600, 2, 10, QLatin1Char('0'))
        .arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString formatSize(qint64 bytes)
{
    const double mb = bytes / (1024.0 * 1024.0);
    if (mb >= 1024.0)
        return QString::number(mb / 1024.0, 'f', 2).replace('.', ',') + QStringLiteral(" ГБ");
    return QString::number(mb, 'f', 2).replace('.', ',') + QStringLiteral(" МБ");
}
