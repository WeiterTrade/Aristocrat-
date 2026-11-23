#pragma once
#include "core/Task.h"

#include <QWidget>

// «Инструменты»: восстановление кассетных записей и проверка модулей.
class ToolboxPage : public QWidget
{
    Q_OBJECT
public:
    explicit ToolboxPage(QWidget *parent = nullptr);

signals:
    void restoreRequested(const QStringList &files, UpscaleOptions::Mode mode, bool vhs);
    // Копирование DVD: список (вход concat:…, имя), без потерь или конвертировать
    void dvdRequested(const QList<QPair<QString, QString>> &titles, int mode, const QString &tempDir);   // mode: 0 MKV без потерь, 1 MP4 для любого плеера, 2 формат из «Конвертировать»
    // Менеджер Adult Empire: видео релиза → раздел «Конвертировать»
    void releaseVideo(const QString &input, const QString &name, const QString &dir, bool lossless, const QString &chaptersMeta);
    void releaseDownload(const QString &pageUrl);

public:
    void openDvd(const QString &path);
    void openRelease(const QString &url = QString(), const QString &video = QString(), const QString &scenesUrl = QString(),
                     int tab = 0);
};
