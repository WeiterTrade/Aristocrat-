#pragma once
#include <QList>
#include <QString>
#include <QStringList>

// Главы (сцены) видео: чтение из файла, запись через FFmpeg (MKV и MP4).
struct Chapter {
    double start = 0;      // секунды
    double end = 0;        // 0 = до следующей главы / до конца
    QString title;         // название (по-русски)
    QString original;      // исходное название (с сайта)
};

namespace Chapters {

QList<Chapter> read(const QString &file, double *duration = nullptr);   // ffprobe -show_chapters
QString metadataFile(const QList<Chapter> &list, double duration);     // текст ;FFMETADATA1
QStringList muxArgs(const QString &input, const QString &metaFile, const QString &output);
// Переупаковка в MKV как в MKVToolNix: без перекодирования, все дорожки, вложения и главы из файла
QStringList remuxArgs(const QString &input, const QString &metaFile, const QString &output);

double parseTime(const QString &text, bool *ok = nullptr);   // 01:22:37 / 1:22:37.500 / 22:37
QString formatTime(double seconds);                           // 01:22:37.000
QString titleCase(const QString &text);                       // Каждое Слово С Большой Буквы
QString toOgm(const QList<Chapter> &list);                    // CHAPTER01=… (формат MKVToolNix)
QList<Chapter> fromOgm(const QString &text);

} // namespace Chapters
