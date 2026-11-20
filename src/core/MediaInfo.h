#pragma once
#include <QList>
#include <QString>

struct StreamTrack {
    int index = 0;        // номер дорожки этого типа (0, 1, 2…)
    QString codec;
    QString language;
    QString title;
    int channels = 0;
    QString label() const;
};

struct MediaInfo {
    bool ok = false;
    QString error;

    QString path;
    QString container;
    double duration = 0.0;     // секунды
    qint64 size = 0;           // байты
    qint64 bitrate = 0;        // бит/с

    bool hasVideo = false;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    QString videoCodec;
    bool interlaced = false;
    double sar = 1.0;          // соотношение сторон пикселя
    int rotation = 0;

    QList<StreamTrack> audioTracks;
    QList<StreamTrack> subtitleTracks;

    // Блокирующий вызов ffprobe. Вызывать не из потока интерфейса.
    static MediaInfo probe(const QString &path);
};
