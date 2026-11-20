#pragma once
#include "Task.h"

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

class ConversionEngine;

// Исполнитель: берёт задачи из общей очереди ConversionEngine и выполняет по одной.
// Ход выполнения читается из FFmpeg (-progress). Нейросетевой апскейл идёт кусками по 10 секунд;
// готовые куски сохраняются, поэтому после остановки работа продолжается с того же места.
class ConversionWorker : public QObject
{
    Q_OBJECT
public:
    ConversionWorker(ConversionEngine *pool, QObject *parent = nullptr);
    ~ConversionWorker() override;

    void begin(const QString &outputDir, bool hwAccel);
    void stop();
    bool isRunning() const { return m_running; }
    TaskPtr current() const { return m_current; }

signals:
    void taskChanged(int id);
    void taskFinished(int id);
    void idle();

private:
    enum class Phase { Idle, Single, AIExtract, AIUpscale, AIEncode, AIMux, Chapters };
    void doneOrChapters();

    void next();
    void startSingle();
    void startAI();
    void aiNextSegment();
    void aiStartUpscale();
    void aiStartEncode();
    void aiStartMux();

    void run(const QString &program, const QStringList &args, Phase phase);
    void onStdout();
    void onFinished(int code, QProcess::ExitStatus status);
    void onUpscaleTick();
    void finishCurrent(ConversionTask::Status status, const QString &msg = QString());
    void setProgress(double p);

    QString workDirFor(const ConversionTask &t) const;
    double segmentLength() const { return 10.0; }

    ConversionEngine *m_pool;
    TaskPtr m_current;
    QProcess *m_proc = nullptr;
    QTimer m_tick;
    Phase m_phase = Phase::Idle;
    bool m_running = false;
    bool m_stopping = false;
    bool m_hw = false;
    QString m_outDir;
    QString m_tempDir;
    QString m_stderr;
    double m_phaseDuration = 0;

    // Состояние нейросетевого апскейла
    QString m_work;
    int m_seg = 0;
    int m_segCount = 0;
    int m_segFrames = 0;
};
