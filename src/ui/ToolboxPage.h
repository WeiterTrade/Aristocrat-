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
    void dvdRequested(const QList<QPair<QString, QString>> &titles, bool lossless);

public:
    void openDvd(const QString &path);
};
