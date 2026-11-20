#pragma once
#include "core/Task.h"

#include <QFrame>

class QLabel;
class QComboBox;
class QPushButton;
class QProgressBar;
class QToolButton;
class QLineEdit;
class QStackedWidget;

// Строка задачи: миниатюра, исходный файл → результат, формат, субтитры, звук, апскейл, кнопка «Конвертировать».
class TaskCard : public QFrame
{
    Q_OBJECT
public:
    explicit TaskCard(const TaskPtr &task, QWidget *parent = nullptr);
    TaskPtr task() const { return m_task; }

    void refresh();            // перечитать данные задачи
    void setInfoLoaded();      // ffprobe завершён

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

signals:
    void convertRequested(const TaskPtr &t);
    void stopRequested();
    void removeRequested(const TaskPtr &t);
    void applyEditToAll(const EditOptions &e);
    void changed();

private:
    void openFormatPopup();
    void openSizeDialog();
    void openEdit(int tab);
    void openTrim();
    void rebuildTrackCombos();
    void renameTarget();
    void loadThumbnail();

    TaskPtr m_task;
    QLabel *m_thumb;
    QLabel *m_srcName;
    QLabel *m_srcFmt, *m_srcRes, *m_srcDur, *m_srcSize;
    QLabel *m_dstName;
    QLabel *m_dstFmt, *m_dstRes, *m_dstDur;
    QToolButton *m_dstSize;
    QToolButton *m_formatBtn;
    QComboBox *m_subs;
    QComboBox *m_audio;
    QComboBox *m_upscale;
    QStackedWidget *m_actionStack;
    QPushButton *m_convert;
    QProgressBar *m_progress;
    QLabel *m_status;
    QPushButton *m_stop;
    QPushButton *m_openFolder;
    QWidget *m_editTools;
    bool m_updatingCombos = false;
};
