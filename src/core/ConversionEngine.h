#pragma once
#include "Task.h"

#include <QObject>
#include <QProcess>
#include <QSet>
#include <QStringList>

class ConversionWorker;

// Очередь конвертации. Несколько исполнителей (ConversionWorker) берут задачи из общей очереди —
// число одновременных задач задаётся в настройках.
class ConversionEngine : public QObject
{
    Q_OBJECT
public:
    explicit ConversionEngine(QObject *parent = nullptr);
    ~ConversionEngine() override;

    void start(const QList<TaskPtr> &tasks, const QString &outputDir, bool hwAccel, bool merge);
    void enqueue(const TaskPtr &task);   // добавить в работающую очередь
    void stop();
    bool isRunning() const { return m_running; }

    // Для исполнителей
    TaskPtr takeNext();
    QString reserveOutputPath(const QString &path);

signals:
    void taskChanged(int id);
    void taskFinished(int id);
    void queueFinished(const QStringList &outputs, int failed);
    void message(const QString &text);

private:
    void onWorkerFinishedTask(int id);
    void onWorkerIdle();
    void startMerge();
    void finishAll();

    QList<ConversionWorker *> m_workers;
    QList<TaskPtr> m_queue;
    QList<TaskPtr> m_all;
    QSet<QString> m_reserved;
    QProcess *m_merge = nullptr;
    bool m_running = false;
    bool m_stopping = false;
    bool m_doMerge = false;
    QString m_outDir;
    QStringList m_outputs;
    int m_failed = 0;
};
