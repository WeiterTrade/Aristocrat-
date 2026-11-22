#pragma once
#include <QString>
#include <QProcessEnvironment>
#include <QStringList>

// Поиск внешних программ: FFmpeg, ffprobe, Real-ESRGAN.
// Сначала ищем рядом с программой (папка tools), потом в системном PATH.
namespace Tools {

QString findExecutable(const QString &name, const QStringList &subdirs = {});
QString ffmpeg();
QString ffprobe();
QString realesrgan();
QString realesrganModelsDir();

QString ytdlp();
QProcessEnvironment utf8Env();   // окружение для yt-dlp: вывод в UTF-8 (иначе русские имена файлов искажаются)
QString deno();
QString rclone();
// Папка совпадает с папкой программы или лежит внутри неё (там храним только саму программу)
bool insideAppDir(const QString &dir);
// Удалить из папки программы хвосты загрузок (.part, .ytdl, фрагменты) — файлы самой программы не трогаются
void cleanAppDir();   // выгрузка в облака (Google Диск, Яндекс Диск, Облако Mail.ru, OneDrive)

bool hasFfmpeg();
bool hasRealesrgan();

// Аппаратное ускорение: возвращает имя кодировщика H.264 (h264_nvenc, h264_qsv, h264_amf)
// или пустую строку, если видеокарта не поддерживается. Проверяется один раз.
QString hardwareH264Encoder();
QString hardwareHevcEncoder();
// Список доступных ускорителей для показа в настройках (NVIDIA NVENC, Intel Quick Sync, AMD AMF)
QStringList hardwareAccelerators();

// Экранирование пути для фильтров FFmpeg (subtitles=, movie=)
QString escapeFilterPath(const QString &path);

} // namespace Tools
