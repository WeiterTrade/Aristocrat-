#pragma once
#include "core/Preset.h"

#include "Frameless.h"

class QComboBox;
class QLineEdit;

// Настройка параметров формата (кодек, разрешение, частота кадров, битрейт).
class PresetEditDialog : public FramedDialog
{
    Q_OBJECT
public:
    explicit PresetEditDialog(const Preset &p, QWidget *parent = nullptr);
    Preset preset() const;

private:
    Preset m_base;
    QLineEdit *m_name;
    QComboBox *m_ext;
    QComboBox *m_vcodec;
    QComboBox *m_res;
    QComboBox *m_fps;
    QComboBox *m_vbr;
    QComboBox *m_acodec;
    QComboBox *m_abr;
};
