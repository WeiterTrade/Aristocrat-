#pragma once
#include "core/ConversionEngine.h"
#include "core/Task.h"

#include <QHash>
#include <QSet>
#include <QWidget>

class QStackedWidget;
class QVBoxLayout;
class QPushButton;
class QComboBox;
class QCheckBox;
class QToolButton;
class QLabel;
class QListWidget;
class TaskCard;

// Вкладка «Конвертировать»: список задач, выбор формата для всех, нижняя панель с папкой и кнопкой запуска.
class ConvertPage : public QWidget
{
    Q_OBJECT
public:
    explicit ConvertPage(QWidget *parent = nullptr);
    void addFiles(const QStringList &paths);
    void addFileAndConvert(const QString &file, bool autoStart);
    void addFilesWithUpscale(const QStringList &paths, UpscaleOptions::Mode mode, bool vhs);
    bool isBusy() const { return m_engine->isRunning(); }
    void openFirstTrim();
    void reloadSettings();
    void addDvdTitles(const QList<QPair<QString, QString>> &titles, bool lossless);
    // Видео из Менеджера релизов: своё имя и папка, главы вставляются после конвертации
    void addReleaseVideo(const QString &input, const QString &name, const QString &dir, bool lossless, const QString &chaptersMeta,
                         bool deleteSource = false);   // deleteSource — скачанный файл удалить после успешной обработки

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

public:
    QString releaseStatus(const QString &dir) const;   // видео релиза в обработке (для планировщика)

signals:
    void releaseVideoDone(const QString &dir, bool ok);   // видео релиза обработано (для планировщика)

private:
    QWidget *buildSubBar();
    QWidget *buildBottomBar();
    QWidget *buildConvertedPage();

    void chooseFiles();
    void chooseFolder();
    void loadDvdFolder();
    TaskPtr addTask(const QString &input, const QString &displayName = QString());
    void removeTask(const TaskPtr &t);
    void convertAll();
    void convertOne(const TaskPtr &t);
    void applyGlobalPreset(const Preset &p);
    void updateView();
    void updateGlobalButton();
    void onTaskChanged(int id);
    void onQueueFinished(const QStringList &outputs, int failed);
    void addConvertedEntry(const TaskPtr &t);
    void chooseOutputDir();
    QString outputDir() const;

    ConversionEngine *m_engine;
    QList<TaskPtr> m_tasks;
    QHash<int, TaskCard *> m_cards;
    int m_nextId = 1;
    QSet<int> m_autoStart;

    QStackedWidget *m_stack;
    QVBoxLayout *m_cardLayout;
    QPushButton *m_segConverting;
    QPushButton *m_segConverted;
    QPushButton *m_globalFormat;
    QComboBox *m_outDir;
    QCheckBox *m_merge;
    QToolButton *m_hw;
    QToolButton *m_alarm;
    QPushButton *m_convertAll;
    QLabel *m_statusLine;
    QListWidget *m_converted;
};
