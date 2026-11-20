#pragma once
#include "Task.h"

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

// Очередь конвертации. Задачи выполняются по одной, ход выполнения читается из FFmpeg (-progress).
// Нейросетевой апскейл идёт кусками по 10 секунд; готовые куски сохраняются,
// поэтому после остановки работа продолжается с того же места.
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

signals:
    void taskChanged(int id);
    void taskFinished(int id);
    void queueFinished(const QStringList &outputs, int failed);
    void message(const QString &text);

private:
    enum class Phase { Idle, Single, AIExtract, AIUpscale, AIEncode, AIMux, Merge };

    void next();
    void startSingle();
    void startAI();
    void aiNextSegment();
    void aiStartUpscale();
    void aiStartEncode();
    void aiStartMux();
    void startMerge();

    void run(const QString &program, const QStringList &args, Phase phase);
    void onStdout();
    void onFinished(int code, QProcess::ExitStatus status);
    void onUpscaleTick();
    void finishCurrent(ConversionTask::Status status, const QString &msg = QString());
    void setProgress(double p);

    QString uniquePath(const QString &path) const;
    QString workDirFor(const ConversionTask &t) const;
    double segmentLength() const { return 10.0; }

    QList<TaskPtr> m_queue;
    TaskPtr m_current;
    QProcess *m_proc = nullptr;
    QTimer m_tick;
    Phase m_phase = Phase::Idle;
    bool m_running = false;
    bool m_stopping = false;
    bool m_hw = false;
    bool m_merge = false;
    QString m_outDir;
    QString m_tempDir;
    QStringList m_outputs;
    int m_failed = 0;
    QString m_stderr;
    double m_phaseDuration = 0;

    // Состояние нейросетевого апскейла
    QString m_work;
    int m_seg = 0;
    int m_segCount = 0;
    int m_segFrames = 0;
};
