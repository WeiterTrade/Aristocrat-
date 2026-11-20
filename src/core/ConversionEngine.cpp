#include "ConversionEngine.h"
#include "ConversionWorker.h"
#include "FfmpegCommand.h"
#include "Tools.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

ConversionEngine::ConversionEngine(QObject *parent)
    : QObject(parent)
{
}

ConversionEngine::~ConversionEngine()
{
    m_stopping = true;
    for (ConversionWorker *w : m_workers)
        w->disconnect(this);
    if (m_merge) {
        m_merge->disconnect(this);
        m_merge->kill();
        m_merge->waitForFinished(2000);
    }
}

void ConversionEngine::start(const QList<TaskPtr> &tasks, const QString &outputDir, bool hwAccel, bool merge)
{
    if (m_running)
        return;
    m_queue.clear();
    m_all.clear();
    m_reserved.clear();
    for (const TaskPtr &t : tasks) {
        if (t->status == ConversionTask::Running)
            continue;
        t->status = ConversionTask::Waiting;
        t->progress = 0;
        t->message = QStringLiteral("В очереди");
        m_queue << t;
        m_all << t;
        emit taskChanged(t->id);
    }
    if (m_queue.isEmpty())
        return;
    m_outDir = outputDir;
    QDir().mkpath(m_outDir);
    m_doMerge = merge;
    m_outputs.clear();
    m_failed = 0;
    m_running = true;
    m_stopping = false;

    // Склейка требует порядка — тогда выполняем по одной
    const int maxTasks = merge ? 1 : qBound(1, QSettings().value("convert/maxTasks", 2).toInt(), 6);
    while (m_workers.size() < maxTasks) {
        auto *w = new ConversionWorker(this, this);
        connect(w, &ConversionWorker::taskChanged, this, &ConversionEngine::taskChanged);
        connect(w, &ConversionWorker::taskFinished, this, &ConversionEngine::onWorkerFinishedTask);
        connect(w, &ConversionWorker::idle, this, &ConversionEngine::onWorkerIdle);
        m_workers << w;
    }
    for (int i = 0; i < maxTasks && i < m_workers.size(); ++i)
        m_workers[i]->begin(m_outDir, hwAccel);
}

void ConversionEngine::enqueue(const TaskPtr &task)
{
    if (!m_running || m_queue.contains(task) || task->status == ConversionTask::Running)
        return;
    task->status = ConversionTask::Waiting;
    task->progress = 0;
    task->message = QStringLiteral("В очереди");
    m_queue << task;
    if (!m_all.contains(task))
        m_all << task;
    emit taskChanged(task->id);
    // Если есть свободный исполнитель — запускаем
    for (ConversionWorker *w : m_workers) {
        if (!w->isRunning()) {
            w->begin(m_outDir, QSettings().value("hardwareAccel", false).toBool());
            break;
        }
    }
}

void ConversionEngine::stop()
{
    if (!m_running)
        return;
    m_stopping = true;
    for (const TaskPtr &t : m_queue) {
        t->status = ConversionTask::Ready;
        t->message.clear();
        emit taskChanged(t->id);
    }
    m_queue.clear();
    if (m_merge)
        m_merge->kill();
    bool any = false;
    for (ConversionWorker *w : m_workers) {
        if (w->isRunning()) {
            any = true;
            w->stop();
        }
    }
    if (!any && !m_merge)
        finishAll();
}

TaskPtr ConversionEngine::takeNext()
{
    if (m_stopping || m_queue.isEmpty())
        return {};
    return m_queue.takeFirst();
}

QString ConversionEngine::reserveOutputPath(const QString &path)
{
    QFileInfo fi(path);
    QString p = path;
    for (int i = 1; (QFile::exists(p) || m_reserved.contains(p)) && i < 10000; ++i)
        p = fi.dir().filePath(QStringLiteral("%1 (%2).%3").arg(fi.completeBaseName()).arg(i).arg(fi.suffix()));
    m_reserved.insert(p);
    return p;
}

void ConversionEngine::onWorkerFinishedTask(int id)
{
    for (const TaskPtr &t : m_all) {
        if (t->id != id)
            continue;
        if (t->status == ConversionTask::Done) {
            m_outputs << t->outputPath;
            // Удалить исходник после конвертации (в корзину, если это возможно)
            if (QSettings().value("convert/deleteSource", false).toBool() && !t->input.startsWith(QLatin1String("concat:"))) {
                QFile f(t->input);
                if (!f.moveToTrash())
                    f.remove();
            }
        } else if (t->status == ConversionTask::Failed) {
            ++m_failed;
        }
    }
    emit taskFinished(id);
}

void ConversionEngine::onWorkerIdle()
{
    for (ConversionWorker *w : m_workers)
        if (w->isRunning())
            return;
    if (!m_stopping && m_doMerge && m_outputs.size() > 1 && !m_merge) {
        startMerge();
        return;
    }
    finishAll();
}

void ConversionEngine::finishAll()
{
    if (!m_running)
        return;
    m_running = false;
    emit queueFinished(m_outputs, m_failed);
}

void ConversionEngine::startMerge()
{
    const QString tmp = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/aristocrat-vc");
    QDir().mkpath(tmp);
    const QString list = QDir(tmp).filePath("merge_list.txt");
    QFile f(list);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream ts(&f);
        ts.setCodec("UTF-8");
        for (QString p : m_outputs) {
            p.replace('\'', QStringLiteral("'\\''"));
            ts << "file '" << p << "'\n";
        }
    }
    const QString ext = QFileInfo(m_outputs.first()).suffix();
    const QString out = reserveOutputPath(QDir(m_outDir).filePath(
        QStringLiteral("Объединённое видео %1.%2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh-mm"), ext)));
    emit message(QStringLiteral("Объединение видео…"));
    m_merge = new QProcess(this);
    connect(m_merge, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, out](int code, QProcess::ExitStatus st) {
        m_merge->deleteLater();
        m_merge = nullptr;
        if (st == QProcess::NormalExit && code == 0) {
            m_outputs << out;
            emit message(QStringLiteral("Видео объединены: %1").arg(QFileInfo(out).fileName()));
        } else {
            emit message(QStringLiteral("Не удалось объединить видео: для склейки у всех файлов должен быть одинаковый формат."));
        }
        finishAll();
    });
    m_merge->start(Tools::ffmpeg(), FfmpegCommand::mergeFiles(list, out));
}
