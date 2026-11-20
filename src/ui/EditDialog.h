#pragma once
#include "core/Task.h"

#include <QDialog>
#include <QTimer>

class PreviewLabel;
class QSlider;
class QSpinBox;
class QComboBox;
class QLabel;
class QCheckBox;
class QLineEdit;
class QTabWidget;
class QRadioButton;

// Редактор: Кадрирование / Эффекты / Водяной знак / Субтитры / Аудио.
class EditDialog : public QDialog
{
    Q_OBJECT
public:
    EditDialog(const TaskPtr &task, int initialTab, QWidget *parent = nullptr);
    EditOptions options() const { return m_opt; }
    bool applyToAll() const { return m_applyAll; }

private:
    QWidget *buildCropTab();
    QWidget *buildEffectTab();
    QWidget *buildWatermarkTab();
    QWidget *buildSubtitleTab();
    QWidget *buildAudioTab();

    void changed();                 // отложенное обновление предпросмотра
    void refreshPreviews(bool original);
    void syncCropFields();
    void applyAspect();
    QSize sourceSize() const;

    TaskPtr m_task;
    EditOptions m_opt;
    bool m_applyAll = false;
    int m_generation = 0;
    QTimer m_debounce;

    QTabWidget *m_tabs;
    PreviewLabel *m_orig;
    PreviewLabel *m_out;
    QSlider *m_time;
    QLabel *m_timeLabel;

    QSpinBox *m_cropW = nullptr;
    QSpinBox *m_cropH = nullptr;
    QSpinBox *m_cropX = nullptr;
    QSpinBox *m_cropY = nullptr;
    QComboBox *m_aspect = nullptr;
    QLabel *m_rotLabel = nullptr;

    QSlider *m_bright = nullptr;
    QSlider *m_contrast = nullptr;
    QSlider *m_sat = nullptr;
    QCheckBox *m_deint = nullptr;
    QCheckBox *m_denoise = nullptr;

    QRadioButton *m_wmNone = nullptr;
    QRadioButton *m_wmText = nullptr;
    QRadioButton *m_wmImage = nullptr;
    QLineEdit *m_wmTextEdit = nullptr;
    QLineEdit *m_wmImagePath = nullptr;
    QComboBox *m_wmPos = nullptr;
    QSlider *m_wmOpacity = nullptr;
    QSpinBox *m_wmSize = nullptr;

    QLineEdit *m_subFile = nullptr;
    QComboBox *m_subTrack = nullptr;

    QComboBox *m_audioTrack = nullptr;
    QSlider *m_volume = nullptr;
    QLabel *m_volumeLabel = nullptr;
};
