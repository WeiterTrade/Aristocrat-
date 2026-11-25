#pragma once
#include "MediaInfo.h"
#include "Preset.h"

#include <QImage>
#include <QRect>
#include <QSharedPointer>
#include <QSize>
#include <QString>

// Правки ролика: обрезка по времени, кадрирование, эффекты, водяной знак, субтитры, звук.
struct EditOptions {
    double trimStart = 0.0;    // секунды
    double trimEnd = 0.0;      // 0 = до конца

    QRect crop;                // пустой = без кадрирования
    int fit = 0;               // 0 — как есть (с полями), 1 — во весь экран (заполнить кадр), 2 — растянуть
    bool autoBars = false;     // чёрные поля найдены автоматически (для «Применить ко всем» ищутся заново)
    int rotate = 0;            // 0, 90, 180, 270
    bool flipH = false;
    bool flipV = false;

    int brightness = 0;        // -100..100
    int contrast = 0;          // -100..100
    int saturation = 0;        // -100..100
    bool deinterlace = false;
    bool denoise = false;

    QString watermarkText;
    QString watermarkImage;
    int watermarkPos = 3;      // 0 лев.верх, 1 прав.верх, 2 лев.низ, 3 прав.низ, 4 центр
    int watermarkOpacity = 70; // %
    int watermarkSize = 24;    // размер шрифта / % ширины для картинки

    QString subtitleFile;      // внешний файл .srt/.ass, вшивается в кадр
    int subtitleTrack = -1;    // встроенная дорожка субтитров, -1 = нет

    int audioTrack = 0;        // -1 = без звука
    int volume = 100;          // %

    bool isEmpty() const;
    bool hasVideoFilters() const;
};

// Наш апскейл — функции нет в оригинале.
struct UpscaleOptions {
    enum Mode { None = 0, Fast1080 = 1, Fast4K = 2, AI1080 = 3, AI4K = 4, Fast720 = 5, AI1080Film = 6, AI4KFilm = 7 };
    Mode mode = None;
    bool vhsRestore = false;    // устранение чересстрочности, шумоподавление, обрезка полосы снизу
    QString model = QStringLiteral("realesr-animevideov3"); // или realesrgan-x4plus
    bool isAI() const { return mode == AI1080 || mode == AI4K || mode == AI1080Film || mode == AI4KFilm; }
    bool is4K() const { return mode == Fast4K || mode == AI4K || mode == AI4KFilm; }
    // Модель: «кино» — realesrgan-x4plus (живые съёмки, старые фильмы), иначе — быстрая модель для мультфильмов
    QString aiModel() const { return (mode == AI1080Film || mode == AI4KFilm) ? QStringLiteral("realesrgan-x4plus") : model; }
    int targetHeight() const { return is4K() ? 2160 : mode == Fast720 ? 720 : (mode == None ? 0 : 1080); }
    int targetWidth() const { return is4K() ? 3840 : mode == Fast720 ? 1280 : (mode == None ? 0 : 1920); }
    static QString modeName(Mode m);
};

struct ConversionTask {
    enum Status { Ready, Waiting, Running, Done, Failed, Cancelled };

    int id = 0;
    QString input;
    MediaInfo info;
    bool probed = false;
    QImage thumbnail;

    Preset preset;
    QString outputName;       // имя без расширения
    QString outputDir;        // своя папка (Менеджер релизов), пусто — общая
    QString chaptersMeta;     // файл глав FFMETADATA — вставить в результат после конвертации
    bool improveLowRes = false;
    QString tempDir;          // DVD со снятой защитой: временные файлы, удаляются после успешной конвертации
    bool deleteInputAfter = false;  // скачанный исходник видео релиза — удалить после успешной обработки  // видео релиза: если ниже 720p — обрезать рамки, поднять до 720p, сжать в H.265
    EditOptions edit;
    UpscaleOptions upscale;

    int customVBitrate = 0;   // из окна «Размер файла», 0 = как в пресете
    QSize customResolution;   // пустой = как в пресете

    Status status = Ready;
    double progress = 0.0;    // 0..1
    QString message;
    QString outputPath;

    double effectiveDuration() const;
    QSize outputSize() const;          // итоговое разрешение кадра
    int videoBitrateKbps() const;      // 0 = по качеству (CRF)
    qint64 estimatedBytes() const;
    QString outputFileName() const;    // имя с расширением
};

using TaskPtr = QSharedPointer<ConversionTask>;

QString formatDuration(double seconds);
QString formatSize(qint64 bytes);
