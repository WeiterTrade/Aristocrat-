#include "FfmpegCommand.h"
#include "Tools.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QtMath>

namespace FfmpegCommand {

static int even(int v) { return v & ~1; }

static QString fontFile()
{
#ifdef Q_OS_WIN
    const QStringList c = {"C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/tahoma.ttf"};
#else
    const QStringList c = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                           "/usr/share/fonts/TTF/DejaVuSans.ttf",
                           "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                           "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                           "/usr/share/fonts/noto/NotoSans-Regular.ttf"};
#endif
    for (const QString &f : c)
        if (QFile::exists(f))
            return f;
    return {};
}

static QString positionExpr(int pos, bool text)
{
    const QString w = text ? "text_w" : "overlay_w";
    const QString h = text ? "text_h" : "overlay_h";
    const QString W = text ? "w" : "main_w";
    const QString H = text ? "h" : "main_h";
    switch (pos) {
    case 0: return QStringLiteral("x=20:y=20");
    case 1: return QStringLiteral("x=%1-%2-20:y=20").arg(W, w);
    case 2: return QStringLiteral("x=20:y=%1-%2-20").arg(H, h);
    case 4: return QStringLiteral("x=(%1-%2)/2:y=(%3-%4)/2").arg(W, w, H, h);
    default: return QStringLiteral("x=%1-%2-20:y=%3-%4-20").arg(W, w, H, h);
    }
}

static bool vhsDoublesRate(const ConversionTask &t)
{
    return t.upscale.vhsRestore || t.edit.deinterlace;
}

// Фильтры восстановления и правки, которые идут ДО масштабирования.
static QStringList preFilters(const ConversionTask &t)
{
    QStringList f;
    const EditOptions &e = t.edit;
    if (t.upscale.vhsRestore || e.deinterlace)
        f << QStringLiteral("bwdif=mode=send_field:parity=auto:deint=%1").arg(t.upscale.vhsRestore ? "all" : "interlaced");
    if (!e.crop.isNull())
        f << QStringLiteral("crop=%1:%2:%3:%4").arg(even(e.crop.width())).arg(even(e.crop.height())).arg(e.crop.x()).arg(e.crop.y());
    else if (t.upscale.vhsRestore)
        f << QStringLiteral("crop=iw-16:ih-12:8:0"); // дрожащая полоса внизу кадра у VHS
    if (t.upscale.vhsRestore || e.denoise)
        f << QStringLiteral("hqdn3d=3:2:4:3");
    if (e.rotate == 90) f << QStringLiteral("transpose=1");
    else if (e.rotate == 270) f << QStringLiteral("transpose=2");
    else if (e.rotate == 180) f << QStringLiteral("hflip") << QStringLiteral("vflip");
    if (e.flipH) f << QStringLiteral("hflip");
    if (e.flipV) f << QStringLiteral("vflip");
    if (e.brightness || e.contrast || e.saturation) {
        f << QStringLiteral("eq=brightness=%1:contrast=%2:saturation=%3")
                 .arg(e.brightness / 100.0 * 0.3, 0, 'f', 3)
                 .arg(1.0 + e.contrast / 100.0, 0, 'f', 3)
                 .arg(1.0 + e.saturation / 100.0, 0, 'f', 3);
    }
    // Квадратные пиксели (у оцифровок VHS/DVD пиксели не квадратные)
    if (qAbs(t.info.sar - 1.0) > 0.01)
        f << QStringLiteral("scale=trunc(iw*sar/2)*2:ih,setsar=1");
    return f;
}

static QStringList scaleFilters(const ConversionTask &t)
{
    QStringList f;
    const QSize o = t.outputSize();
    const bool needScale = o.isValid() && !o.isEmpty()
        && (t.upscale.mode != UpscaleOptions::None || (t.customResolution.isValid() && !t.customResolution.isEmpty())
            || (t.preset.width > 0 && t.preset.height > 0));
    if (needScale) {
        const int W = even(o.width()), H = even(o.height());
        f << QStringLiteral("scale=%1:%2:force_original_aspect_ratio=decrease:flags=lanczos").arg(W).arg(H)
          << QStringLiteral("pad=%1:%2:(ow-iw)/2:(oh-ih)/2:black").arg(W).arg(H)
          << QStringLiteral("setsar=1");
    }
    if (!t.preset.isAudioOnly() && t.preset.fps > 0)
        f << QStringLiteral("fps=%1").arg(t.preset.fps);
    return f;
}

static QStringList overlayFilters(const ConversionTask &t, const QString &tempDir)
{
    QStringList f;
    const EditOptions &e = t.edit;
    if (!e.subtitleFile.isEmpty())
        f << QStringLiteral("subtitles=filename=%1").arg(Tools::escapeFilterPath(e.subtitleFile));
    else if (e.subtitleTrack >= 0)
        f << QStringLiteral("subtitles=filename=%1:si=%2").arg(Tools::escapeFilterPath(t.input)).arg(e.subtitleTrack);

    if (!e.watermarkText.isEmpty() && !tempDir.isEmpty()) {
        // Текст пишем в файл — так не нужно экранировать спецсимволы.
        QDir().mkpath(tempDir);
        const QString txt = tempDir + QStringLiteral("/watermark_%1.txt").arg(t.id);
        QFile file(txt);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(e.watermarkText.toUtf8());
            file.close();
        }
        QString d = QStringLiteral("drawtext=textfile=%1:fontsize=%2:fontcolor=white@%3:shadowcolor=black@0.6:shadowx=2:shadowy=2:%4")
                        .arg(Tools::escapeFilterPath(txt))
                        .arg(qMax(8, e.watermarkSize))
                        .arg(e.watermarkOpacity / 100.0, 0, 'f', 2)
                        .arg(positionExpr(e.watermarkPos, true));
        const QString font = fontFile();
        if (!font.isEmpty())
            d += QStringLiteral(":fontfile=") + Tools::escapeFilterPath(font);
        f << d;
    }
    return f;
}

QStringList videoFilters(const ConversionTask &t, Stage stage, const QString &tempDir)
{
    QStringList f;
    if (stage == Stage::Full || stage == Stage::PreUpscale)
        f << preFilters(t);
    if (stage == Stage::Full || stage == Stage::PostUpscale)
        f << scaleFilters(t) << overlayFilters(t, tempDir);
    return f;
}

FilterGraph buildGraph(const ConversionTask &t, Stage stage, const QString &tempDir, int inputIndex)
{
    FilterGraph g;
    QStringList chain = videoFilters(t, stage, tempDir);
    const bool image = stage != Stage::PreUpscale && !t.edit.watermarkImage.isEmpty()
                       && QFile::exists(t.edit.watermarkImage);
    if (chain.isEmpty() && !image)
        return g;
    g.empty = false;
    if (chain.isEmpty())
        chain << QStringLiteral("null");
    if (!image) {
        g.complex = QStringLiteral("[%1:v]").arg(inputIndex) + chain.join(',') + QStringLiteral("[vout]");
        return g;
    }
    g.extraInputs << QStringLiteral("-i") << t.edit.watermarkImage;
    const QSize o = t.outputSize();
    const int baseW = o.isValid() && o.width() > 0 ? o.width() : qMax(320, t.info.width);
    const int wmW = qMax(16, even(baseW * qBound(2, t.edit.watermarkSize, 80) / 100));
    g.complex = QStringLiteral("[%1:v]%2[base];[%3:v]scale=%4:-1,format=rgba,colorchannelmixer=aa=%5[wm];[base][wm]overlay=%6[vout]")
                    .arg(inputIndex)
                    .arg(chain.join(','))
                    .arg(inputIndex + 1)
                    .arg(wmW)
                    .arg(t.edit.watermarkOpacity / 100.0, 0, 'f', 2)
                    .arg(positionExpr(t.edit.watermarkPos, false));
    return g;
}

static int estimatedVideoKbps(const ConversionTask &t)
{
    const int v = t.videoBitrateKbps();
    if (v > 0)
        return v;
    const double dur = qMax(1.0, t.effectiveDuration());
    const int total = int(t.estimatedBytes() * 8.0 / 1000.0 / dur);
    return qMax(800, total - 192);
}

QStringList videoCodecArgs(const ConversionTask &t, bool hwAccel)
{
    QStringList a;
    const QString vc = t.preset.vcodec;
    const int br = t.videoBitrateKbps();
    const QString ext = t.preset.ext.toLower();

    auto bitrate = [&](int kbps) {
        a << "-b:v" << QStringLiteral("%1k").arg(kbps)
          << "-maxrate" << QStringLiteral("%1k").arg(int(kbps * 1.5))
          << "-bufsize" << QStringLiteral("%1k").arg(kbps * 2);
    };

    QString hw;
    if (hwAccel && vc == QLatin1String("libx264"))
        hw = Tools::hardwareH264Encoder();
    else if (hwAccel && vc == QLatin1String("libx265"))
        hw = Tools::hardwareHevcEncoder();

    if (!hw.isEmpty()) {
        a << "-c:v" << hw;
        bitrate(estimatedVideoKbps(t));
        a << "-pix_fmt" << (hw.contains("nvenc") ? "yuv420p" : "nv12");
    } else if (vc == QLatin1String("libx264")) {
        a << "-c:v" << "libx264" << "-preset" << "medium";
        if (br > 0) bitrate(br); else a << "-crf" << "20";
        a << "-pix_fmt" << "yuv420p";
    } else if (vc == QLatin1String("libx265")) {
        a << "-c:v" << "libx265" << "-preset" << "medium";
        if (br > 0) bitrate(br); else a << "-crf" << "24";
        a << "-pix_fmt" << "yuv420p";
    } else if (vc == QLatin1String("mpeg4")) {
        a << "-c:v" << "mpeg4";
        if (br > 0) bitrate(br); else a << "-q:v" << "3";
        if (ext == QLatin1String("avi")) a << "-vtag" << "XVID";
    } else if (vc == QLatin1String("libvpx-vp9")) {
        a << "-c:v" << "libvpx-vp9" << "-row-mt" << "1" << "-deadline" << "good" << "-cpu-used" << "4";
        if (br > 0) a << "-b:v" << QStringLiteral("%1k").arg(br); else a << "-crf" << "32" << "-b:v" << "0";
    } else if (vc == QLatin1String("mpeg2video")) {
        a << "-c:v" << "mpeg2video" << "-b:v" << QStringLiteral("%1k").arg(br > 0 ? br : 6000)
          << "-maxrate" << "9000k" << "-bufsize" << "1835k";
    } else if (vc == QLatin1String("wmv2")) {
        a << "-c:v" << "wmv2" << "-b:v" << QStringLiteral("%1k").arg(br > 0 ? br : 4000);
    } else {
        a << "-c:v" << vc;
        if (br > 0) a << "-b:v" << QStringLiteral("%1k").arg(br);
    }

    if ((vc == QLatin1String("libx265") || hw.startsWith("hevc"))
        && (ext == QLatin1String("mp4") || ext == QLatin1String("mov") || ext == QLatin1String("m4v")))
        a << "-tag:v" << "hvc1";
    return a;
}

QStringList audioCodecArgs(const ConversionTask &t)
{
    QStringList a;
    if (t.edit.audioTrack < 0 || t.info.audioTracks.isEmpty()) {
        a << "-an";
        return a;
    }
    const QString ac = t.preset.acodec;
    a << "-c:a" << ac;
    const bool lossless = ac == QLatin1String("pcm_s16le") || ac == QLatin1String("flac");
    if (!lossless) {
        int br = t.preset.abitrate > 0 ? t.preset.abitrate : 192;
        if (ac == QLatin1String("mp2"))
            br = qMax(br, 224);
        a << "-b:a" << QStringLiteral("%1k").arg(br);
    }
    const int ch = t.info.audioTracks.value(qMax(0, t.edit.audioTrack)).channels;
    if (ch > 2 && (ac == QLatin1String("libmp3lame") || ac == QLatin1String("wmav2") || ac == QLatin1String("mp2")
                   || ac == QLatin1String("pcm_s16le")))
        a << "-ac" << "2";
    if (t.edit.volume != 100)
        a << "-af" << QStringLiteral("volume=%1").arg(t.edit.volume / 100.0, 0, 'f', 2);
    return a;
}

static QStringList common()
{
    return {"-hide_banner", "-nostdin", "-y", "-loglevel", "error", "-progress", "pipe:1", "-nostats"};
}

static QStringList trimInput(const ConversionTask &t)
{
    QStringList a;
    if (t.edit.trimStart > 0)
        a << "-ss" << QString::number(t.edit.trimStart, 'f', 3);
    return a;
}

static QStringList trimDuration(const ConversionTask &t)
{
    QStringList a;
    if (t.edit.trimEnd > 0 || t.edit.trimStart > 0)
        a << "-t" << QString::number(t.effectiveDuration(), 'f', 3);
    return a;
}

static QStringList containerArgs(const ConversionTask &t)
{
    const QString ext = t.preset.ext.toLower();
    if (ext == QLatin1String("mp4") || ext == QLatin1String("mov") || ext == QLatin1String("m4a") || ext == QLatin1String("m4v"))
        return {"-movflags", "+faststart"};
    return {};
}

QStringList singlePass(const ConversionTask &t, const QString &outPath, bool hwAccel, const QString &tempDir)
{
    QStringList a = common();
    a << trimInput(t) << "-i" << t.input;

    const bool video = !t.preset.isAudioOnly() && t.info.hasVideo;
    FilterGraph g;
    if (video)
        g = buildGraph(t, Stage::Full, tempDir);
    a << g.extraInputs << trimDuration(t);

    if (video) {
        if (!g.empty) a << "-filter_complex" << g.complex << "-map" << "[vout]";
        else a << "-map" << "0:v:0";
        a << videoCodecArgs(t, hwAccel);
    } else {
        a << "-vn";
    }
    if (t.edit.audioTrack >= 0 && !t.info.audioTracks.isEmpty())
        a << "-map" << QStringLiteral("0:a:%1?").arg(t.edit.audioTrack);
    a << audioCodecArgs(t) << "-sn" << containerArgs(t) << outPath;
    return a;
}

double preUpscaleFps(const ConversionTask &t)
{
    const double fps = t.info.fps > 0 ? t.info.fps : 25.0;
    return vhsDoublesRate(t) ? fps * 2.0 : fps;
}

int aiScaleFactor(const ConversionTask &t)
{
    if (t.upscale.model == QLatin1String("realesrgan-x4plus"))
        return 4;
    int srcH = t.edit.crop.isNull() ? t.info.height : t.edit.crop.height();
    if (srcH <= 0) srcH = 480;
    const int s = int(qCeil(double(t.upscale.targetHeight()) / srcH));
    return qBound(2, s, 4);
}

QStringList aiExtractFrames(const ConversionTask &t, double start, int frames, const QString &pattern, const QString &tempDir)
{
    Q_UNUSED(tempDir)
    QStringList a = common();
    a << "-ss" << QString::number(t.edit.trimStart + start, 'f', 3) << "-i" << t.input;
    const QStringList f = videoFilters(t, Stage::PreUpscale, QString());
    if (!f.isEmpty())
        a << "-vf" << f.join(',');
    a << "-an" << "-sn" << "-frames:v" << QString::number(frames) << "-q:v" << "2" << pattern;
    return a;
}

QStringList aiUpscale(const ConversionTask &t, const QString &inDir, const QString &outDir)
{
    QStringList a;
    a << "-i" << inDir << "-o" << outDir << "-n" << t.upscale.model
      << "-s" << QString::number(aiScaleFactor(t)) << "-f" << "jpg";
    const QString models = Tools::realesrganModelsDir();
    if (!models.isEmpty() && QDir(models).exists())
        a << "-m" << models;
    return a;
}

QStringList aiEncodeSegment(const ConversionTask &t, const QString &framesPattern, const QString &outSeg, bool hwAccel)
{
    QStringList a = common();
    a << "-framerate" << QString::number(preUpscaleFps(t), 'f', 3) << "-i" << framesPattern;
    // Масштаб до целевого размера, но без субтитров/водяного знака (они — при сборке).
    QStringList f = scaleFilters(t);
    if (!f.isEmpty())
        a << "-vf" << f.join(',');
    ConversionTask copy = t;
    if (copy.preset.vcodec.isEmpty())
        copy.preset.vcodec = QStringLiteral("libx264");
    a << videoCodecArgs(copy, hwAccel) << "-an" << outSeg;
    return a;
}

QStringList aiFinalMux(const ConversionTask &t, const QString &concatList, const QString &outPath, bool hwAccel, const QString &tempDir)
{
    QStringList a = common();
    a << "-f" << "concat" << "-safe" << "0" << "-i" << concatList;
    a << trimInput(t) << "-i" << t.input;

    // Субтитры и водяной знак накладываются на готовую картинку — тогда нужно перекодирование.
    ConversionTask post = t;
    post.upscale.mode = UpscaleOptions::None;   // масштаб уже сделан
    post.customResolution = QSize();
    post.preset.width = post.preset.height = 0;
    FilterGraph g = buildGraph(post, Stage::PostUpscale, tempDir, 0);
    // Если оверлеев нет — копируем видеопоток без потерь.
    if (!g.empty) {
        // buildGraph ссылается на вход картинки как [1:v], а у нас вход 1 — исходник, поэтому сдвигаем.
        if (!g.extraInputs.isEmpty()) {
            a << g.extraInputs;
            g.complex.replace(QStringLiteral("[1:v]scale"), QStringLiteral("[2:v]scale"));
        }
        a << "-filter_complex" << g.complex << "-map" << "[vout]" << videoCodecArgs(t, hwAccel);
    } else {
        a << "-map" << "0:v:0" << "-c:v" << "copy";
    }
    a << trimDuration(t);
    if (t.edit.audioTrack >= 0 && !t.info.audioTracks.isEmpty())
        a << "-map" << QStringLiteral("1:a:%1?").arg(t.edit.audioTrack);
    a << audioCodecArgs(t) << "-shortest" << containerArgs(t) << outPath;
    return a;
}

QStringList mergeFiles(const QString &concatList, const QString &outPath)
{
    QStringList a = common();
    a << "-f" << "concat" << "-safe" << "0" << "-i" << concatList << "-c" << "copy" << outPath;
    return a;
}

QStringList previewFrame(const ConversionTask &t, double at, bool withFilters, int maxWidth, const QString &outJpg, const QString &tempDir)
{
    QStringList a = {"-hide_banner", "-nostdin", "-y", "-loglevel", "error"};
    a << "-ss" << QString::number(qMax(0.0, at), 'f', 3) << "-i" << t.input;
    QStringList scale = {QStringLiteral("scale='min(%1,iw)':-2").arg(maxWidth)};
    if (withFilters) {
        ConversionTask p = t;
        p.upscale.mode = UpscaleOptions::None;  // для предпросмотра без увеличения
        p.customResolution = QSize();
        p.preset.width = p.preset.height = 0;
        FilterGraph g = buildGraph(p, Stage::Full, tempDir);
        if (!g.empty) {
            a << g.extraInputs << "-filter_complex" << (g.complex.replace("[vout]", "[pv]") + ";[pv]" + scale.first() + "[vout]")
              << "-map" << "[vout]";
        } else {
            a << "-vf" << scale.first();
        }
    } else {
        a << "-vf" << scale.first();
    }
    a << "-frames:v" << "1" << "-q:v" << "3" << outJpg;
    return a;
}

QString progressArgsNote() { return QStringLiteral("-progress pipe:1"); }

} // namespace FfmpegCommand
