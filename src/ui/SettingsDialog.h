#pragma once
#include "Frameless.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTabWidget;

// Настройки: Общие / Конвертация / Загрузка / Расположение.
class SettingsDialog : public FramedDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr, int tab = 0);
    bool languageChanged() const { return m_langChanged; }

private:
    QWidget *buildGeneral();
    QWidget *buildConvert();
    QWidget *buildDownload();
    QWidget *buildLocation();
    void load();
    void save();
    void resetPage();

    QTabWidget *m_tabs;
    bool m_langChanged = false;

    QCheckBox *m_sound;
    QComboBox *m_language;

    QCheckBox *m_deleteSource;
    QCheckBox *m_chapters;
    QCheckBox *m_gpu;
    QLabel *m_gpuInfo;
    QComboBox *m_dvd;
    QComboBox *m_convTasks;

    QComboBox *m_dlTasks;
    QCheckBox *m_oneClick;
    QComboBox *m_ocType;
    QComboBox *m_ocQuality;
    QCheckBox *m_subs;
    QComboBox *m_subLang;
    QCheckBox *m_embedSubs;
    QCheckBox *m_resume;
    QLabel *m_ytdlpInfo;

    QLineEdit *m_dirConverted;
    QLineEdit *m_dirDownloaded;
    QLineEdit *m_dirBurned;
};
