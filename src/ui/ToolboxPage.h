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
};
