#pragma once
#include "Frameless.h"
#include "core/Chapters.h"

#include <QProcess>

class QLineEdit;
class QTableWidget;
class QCheckBox;
class QLabel;
class QPushButton;

// Редактор глав (сцен), как в MKVToolNix: загрузка сцен с Adult Empire, перевод на русский,
// правка вручную и запись глав в видеофайл.
class ChaptersDialog : public FramedDialog
{
    Q_OBJECT
public:
    ChaptersDialog(const QString &file = QString(), const QString &pageUrl = QString(), QWidget *parent = nullptr);

private:
    void chooseFile();
    void loadFile(const QString &file);
    void fetchScenes();
    void translateTitles();
    void applyCase();
    void setChapters(const QList<Chapter> &list);
    QList<Chapter> chapters(bool *ok = nullptr) const;
    void addRow(const Chapter &c, int at = -1);
    void moveRow(int delta);
    void save();
    void importOgm();
    void exportOgm();
    void setBusy(bool busy, const QString &text = QString());

    QLineEdit *m_file;
    QLineEdit *m_url;
    QTableWidget *m_table;
    QCheckBox *m_caps;
    QCheckBox *m_replace;
    QLabel *m_state;
    QPushButton *m_fetch;
    QPushButton *m_translate;
    QPushButton *m_save;
    double m_duration = 0;
    QProcess *m_proc = nullptr;
};
