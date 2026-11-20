#pragma once
#include "core/Task.h"

#include <QDialog>

class PreviewLabel;
class QSlider;
class QTimeEdit;
class QLabel;

// Обрезка по времени: начало и конец ролика.
class TrimDialog : public QDialog
{
    Q_OBJECT
public:
    TrimDialog(const TaskPtr &task, QWidget *parent = nullptr);
    double start() const;
    double end() const;   // 0 = до конца

private:
    void showFrame();
    void updateInfo();

    TaskPtr m_task;
    PreviewLabel *m_preview;
    QSlider *m_slider;
    QTimeEdit *m_start;
    QTimeEdit *m_end;
    QLabel *m_info;
    int m_generation = 0;
};
