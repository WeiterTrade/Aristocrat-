#pragma once
#include "Task.h"

#include <QStringList>

// Построение командных строк FFmpeg из настроек задачи.
namespace FfmpegCommand {

enum class Stage { Full, PreUpscale, PostUpscale };

struct FilterGraph {
    QStringList extraInputs;   // дополнительные -i (картинка водяного знака)
    QString complex;           // для -filter_complex, выход [vout]
    bool empty = true;
};

// Цепочка видеофильтров для указанного этапа.
QStringList videoFilters(const ConversionTask &t, Stage stage, const QString &tempDir);
FilterGraph buildGraph(const ConversionTask &t, Stage stage, const QString &tempDir, int inputIndex = 0);

QStringList videoCodecArgs(const ConversionTask &t, bool hwAccel);
QStringList audioCodecArgs(const ConversionTask &t);

// Обычная конвертация за один проход.
QStringList singlePass(const ConversionTask &t, const QString &outPath, bool hwAccel, const QString &tempDir);

// Нейросетевой апскейл идёт кусками: кадры → Real-ESRGAN → видеофрагмент → склейка.
double preUpscaleFps(const ConversionTask &t);
int aiScaleFactor(const ConversionTask &t);
QStringList aiExtractFrames(const ConversionTask &t, double start, int frames, const QString &pattern, const QString &tempDir);
QStringList aiUpscale(const ConversionTask &t, const QString &inDir, const QString &outDir);
QStringList aiEncodeSegment(const ConversionTask &t, const QString &framesPattern, const QString &outSeg, bool hwAccel);
QStringList aiFinalMux(const ConversionTask &t, const QString &concatList, const QString &outPath, bool hwAccel, const QString &tempDir);

// Склейка готовых файлов в один.
QStringList mergeFiles(const QString &concatList, const QString &outPath);

// Кадр для предпросмотра (с фильтрами или без).
QStringList previewFrame(const ConversionTask &t, double at, bool withFilters, int maxWidth, const QString &outJpg, const QString &tempDir);

QString progressArgsNote();

// Поиск чёрных полей (рамок) по нескольким кадрам. Пустой прямоугольник — полей нет.
// Вызывать не из потока интерфейса.
QRect detectBlackBars(const ConversionTask &t);

} // namespace FfmpegCommand
