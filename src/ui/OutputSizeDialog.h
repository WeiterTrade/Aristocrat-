#pragma once
#include "core/Task.h"

#include <QDialog>

class QDoubleSpinBox;
class QSlider;
class QComboBox;
class QLabel;

// Окно «Желаемый размер файла»: размер ↔ битрейт ↔ разрешение.
class OutputSizeDialog : public QDialog
{
    Q_OBJECT
public:
    OutputSizeDialog(const TaskPtr &task, QWidget *parent = nullptr);
    int videoBitrate() const { return m_vbr; }
    QSize resolution() const;

private:
    void recalcFromSize();
    void updateLabels();
    double maxSizeMb() const;

    TaskPtr m_task;
    double m_defaultMb = 0;
    int m_vbr = 0;
    QDoubleSpinBox *m_size;
    QSlider *m_slider;
    QComboBox *m_res;
    QLabel *m_bitrate;
    QLabel *m_quality;
};
