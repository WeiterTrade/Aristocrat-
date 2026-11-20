#include "ConversionEngine.h"
#include "FfmpegCommand.h"
#include "Tools.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QtMath>

ConversionEngine::ConversionEngine(QObject *parent)
    : QObject(parent)
{
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &ConversionEngine::onUpscaleTick);
    m_tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/aristocrat-vc");
    QDir().mkpath(m_tempDir);
}

ConversionEngine::~ConversionEngine()
{
    m_stopping = true;
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->waitForFinished(3000);
    }
}

void ConversionEngine::start(const QList<TaskPtr> &tasks, const QString &outputDir, bool hwAccel, bool merge)
{
    if (m_running)
        return;
    m_queue.clear();
    for (const TaskPtr &t : tasks) {
        if (t->status == ConversionTask::Running)
            continue;
        t->status = ConversionTask::Waiting;
        t->progress = 0;
        t->message = QStringLiteral("В очереди");
        m_queue << t;
        emit taskChanged(t->id);
    }
    if (m_queue.isEmpty())
        return;
    m_outDir = outputDir;
    QDir().mkpath(m_outDir);
    m_hw = hwAccel;
    m_merge = merge;
    m_outputs.clear();
    m_failed = 0;
    m_running = true;
    m_stopping = false;
    next();
}

void ConversionEngine::enqueue(const TaskPtr &task)
{
    if (!m_running || m_queue.contains(task) || m_current == task)
        return;
    task->status = ConversionTask::Waiting;
    task->progress = 0;
    task->message = QStringLiteral("В очереди");
    m_queue << task;
    emit taskChanged(task->id);
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
    if (m_proc && m_proc->state() != QProcess::NotRunning)
        m_proc->kill();
    else if (m_current)
        finishCurrent(ConversionTask::Cancelled, QStringLiteral("Остановлено"));
}

void ConversionEngine::next()
{
    m_current.reset();
    if (m_stopping || m_queue.isEmpty()) {
        if (!m_stopping && m_merge && m_outputs.size() > 1) {
            startMerge();
            return;
        }
        m_running = false;
        m_phase = Phase::Idle;
        emit queueFinished(m_outputs, m_failed);
        return;
    }
    m_current = m_queue.takeFirst();
    m_current->status = ConversionTask::Running;
    m_current->progress = 0;
    m_current->message = QStringLiteral("Подготовка…");
    emit taskChanged(m_current->id);

    if (!Tools::hasFfmpeg()) {
        finishCurrent(ConversionTask::Failed, QStringLiteral("Не найден FFmpeg (папка tools рядом с программой)"));
        return;
    }
    if (!m_current->info.ok) {
        finishCurrent(ConversionTask::Failed, m_current->info.error.isEmpty() ? QStringLiteral("Файл не прочитан") : m_current->info.error);
        return;
    }
    m_current->outputPath = uniquePath(QDir(m_outDir).filePath(m_current->outputFileName()));

    if (m_current->upscale.isAI() && m_current->info.hasVideo && !m_current->preset.isAudioOnly())
        startAI();
    else
        startSingle();
}

void ConversionEngine::run(const QString &program, const QStringList &args, Phase phase)
{
    m_phase = phase;
    m_stderr.clear();
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->deleteLater();
    }
    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &ConversionEngine::onStdout);
    connect(m_proc, &QProcess::readyReadStandardError, this, [this] {
        m_stderr += QString::fromUtf8(m_proc->readAllStandardError());
        if (m_stderr.size() > 8000)
            m_stderr = m_stderr.right(8000);
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &ConversionEngine::onFinished);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            onFinished(-1, QProcess::CrashExit);
    });
    m_proc->start(program, args);
}

void ConversionEngine::startSingle()
{
    m_current->message = QStringLiteral("Конвертация…");
    m_phaseDuration = m_current->effectiveDuration();
    emit taskChanged(m_current->id);
    run(Tools::ffmpeg(), FfmpegCommand::singlePass(*m_current, m_current->outputPath, m_hw, m_tempDir), Phase::Single);
}

void ConversionEngine::onStdout()
{
    while (m_proc && m_proc->canReadLine()) {
        const QByteArray line = m_proc->readLine().trimmed();
        qint64 us = -1;
        if (line.startsWith("out_time_us="))
            us = line.mid(12).toLongLong();
        else if (line.startsWith("out_time_ms="))   // в FFmpeg это тоже микросекунды
            us = line.mid(12).toLongLong();
        if (us < 0 || !m_current)
            continue;
        const double sec = us / 1e6;
        if (m_phase == Phase::Single || m_phase == Phase::AIMux) {
            const double frac = m_phaseDuration > 0 ? qBound(0.0, sec / m_phaseDuration, 1.0) : 0.0;
            setProgress(m_phase == Phase::AIMux ? 0.98 + frac * 0.02 : frac);
        } else if (m_phase == Phase::AIExtract || m_phase == Phase::AIEncode) {
            const double segFrac = m_phase == Phase::AIExtract ? 0.0 : 0.9;
            const double within = qBound(0.0, sec / segmentLength(), 1.0) * (m_phase == Phase::AIExtract ? 0.08 : 0.1);
            setProgress(qMin(0.98, (m_seg + segFrac + within) / qMax(1, m_segCount) * 0.98));
        } else if (m_phase == Phase::Merge) {
            // склейка идёт быстро — прогресс не показываем
        }
    }
}

void ConversionEngine::setProgress(double p)
{
    if (!m_current)
        return;
    m_current->progress = qBound(0.0, p, 1.0);
    emit taskChanged(m_current->id);
}

void ConversionEngine::onFinished(int code, QProcess::ExitStatus status)
{
    m_tick.stop();
    const bool ok = status == QProcess::NormalExit && code == 0;

    if (m_phase == Phase::Merge) {
        m_phase = Phase::Idle;
        m_running = false;
        if (ok)
            emit message(QStringLiteral("Видео объединены: %1").arg(QFileInfo(m_outputs.last()).fileName()));
        else
            emit message(QStringLiteral("Не удалось объединить видео: для склейки у всех файлов должен быть одинаковый формат."));
        emit queueFinished(m_outputs, m_failed);
        return;
    }
    if (!m_current)
        return;

    if (m_stopping) {
        if (m_phase == Phase::Single)
            QFile::remove(m_current->outputPath);
        finishCurrent(ConversionTask::Cancelled, m_current->upscale.isAI()
                                                     ? QStringLiteral("Остановлено. Готовые части сохранены — можно продолжить.")
                                                     : QStringLiteral("Остановлено"));
        return;
    }

    if (!ok) {
        QString err = m_stderr.trimmed();
        if (code == -1)
            err = QStringLiteral("Программа не запустилась");
        const QStringList lines = err.split('\n', Qt::SkipEmptyParts);
        if (m_phase == Phase::Single || m_phase == Phase::AIMux)
            QFile::remove(m_current->outputPath);
        finishCurrent(ConversionTask::Failed, lines.isEmpty() ? QStringLiteral("Ошибка FFmpeg") : lines.last().trimmed());
        return;
    }

    switch (m_phase) {
    case Phase::Single:
        finishCurrent(ConversionTask::Done);
        break;
    case Phase::AIExtract:
        aiStartUpscale();
        break;
    case Phase::AIUpscale:
        aiStartEncode();
        break;
    case Phase::AIEncode: {
        const QString seg = QDir(m_work).filePath(QStringLiteral("seg_%1.mkv").arg(m_seg, 5, 10, QLatin1Char('0')));
        QFile marker(seg + QStringLiteral(".ok"));
        marker.open(QIODevice::WriteOnly);
        marker.close();
        QDir(QDir(m_work).filePath("in")).removeRecursively();
        QDir(QDir(m_work).filePath("out")).removeRecursively();
        ++m_seg;
        aiNextSegment();
        break;
    }
    case Phase::AIMux:
        QDir(m_work).removeRecursively();
        finishCurrent(ConversionTask::Done);
        break;
    default:
        break;
    }
}

void ConversionEngine::finishCurrent(ConversionTask::Status status, const QString &msg)
{
    if (!m_current)
        return;
    TaskPtr t = m_current;
    t->status = status;
    if (status == ConversionTask::Done) {
        t->progress = 1.0;
        t->message = QStringLiteral("Готово");
        m_outputs << t->outputPath;
    } else {
        t->message = msg;
        if (status == ConversionTask::Failed)
            ++m_failed;
    }
    m_phase = Phase::Idle;
    emit taskChanged(t->id);
    emit taskFinished(t->id);
    if (m_stopping) {
        m_current.reset();
        m_running = false;
        emit queueFinished(m_outputs, m_failed);
        return;
    }
    next();
}

// ---------------- Нейросетевой апскейл ----------------

QString ConversionEngine::workDirFor(const ConversionTask &t) const
{
    QFileInfo fi(t.input);
    QString key = t.input + QString::number(fi.size()) + QString::number(fi.lastModified().toMSecsSinceEpoch())
                  + t.upscale.model + QString::number(t.upscale.mode) + QString::number(t.upscale.vhsRestore)
                  + t.preset.id + QString::number(t.videoBitrateKbps())
                  + QString::number(t.edit.trimStart) + QString::number(t.edit.trimEnd)
                  + QStringLiteral("%1,%2,%3,%4").arg(t.edit.crop.x()).arg(t.edit.crop.y()).arg(t.edit.crop.width()).arg(t.edit.crop.height())
                  + QString::number(t.edit.rotate) + QString::number(t.edit.brightness) + QString::number(t.edit.contrast)
                  + QString::number(t.edit.saturation) + QString::number(t.edit.denoise) + QString::number(t.edit.deinterlace);
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5).toHex().left(12));
    return QDir(m_outDir).filePath(QStringLiteral(".aristocrat-work/") + hash);
}

void ConversionEngine::startAI()
{
    if (!Tools::hasRealesrgan()) {
        finishCurrent(ConversionTask::Failed,
                      QStringLiteral("Не найден Real-ESRGAN (tools/realesrgan). Выберите быстрый апскейл или установите модуль."));
        return;
    }
    m_work = workDirFor(*m_current);
    QDir().mkpath(m_work);
    const double dur = m_current->effectiveDuration();
    m_segCount = qMax(1, int(qCeil(dur / segmentLength())));
    m_seg = 0;
    aiNextSegment();
}

void ConversionEngine::aiNextSegment()
{
    // Пропускаем уже готовые куски (продолжение после остановки)
    while (m_seg < m_segCount) {
        const QString seg = QDir(m_work).filePath(QStringLiteral("seg_%1.mkv").arg(m_seg, 5, 10, QLatin1Char('0')));
        if (QFile::exists(seg + QStringLiteral(".ok")) && QFile::exists(seg))
            ++m_seg;
        else
            break;
    }
    if (m_seg >= m_segCount) {
        aiStartMux();
        return;
    }
    const double dur = m_current->effectiveDuration();
    const double start = m_seg * segmentLength();
    const double len = qMin(segmentLength(), dur - start);
    m_segFrames = qMax(1, int(qRound(len * FfmpegCommand::preUpscaleFps(*m_current))));

    QDir in(QDir(m_work).filePath("in"));
    in.removeRecursively();
    QDir().mkpath(in.absolutePath());
    QDir(QDir(m_work).filePath("out")).removeRecursively();

    m_current->message = QStringLiteral("Апскейл: часть %1 из %2 — подготовка кадров").arg(m_seg + 1).arg(m_segCount);
    emit taskChanged(m_current->id);
    run(Tools::ffmpeg(),
        FfmpegCommand::aiExtractFrames(*m_current, start, m_segFrames, in.filePath("%08d.jpg"), m_tempDir),
        Phase::AIExtract);
}

void ConversionEngine::aiStartUpscale()
{
    const QString in = QDir(m_work).filePath("in");
    const QString out = QDir(m_work).filePath("out");
    QDir().mkpath(out);
    m_segFrames = qMax(1, int(QDir(in).entryList(QDir::Files).size()));
    m_current->message = QStringLiteral("Апскейл: часть %1 из %2 — нейросеть").arg(m_seg + 1).arg(m_segCount);
    emit taskChanged(m_current->id);
    m_tick.start();
    run(Tools::realesrgan(), FfmpegCommand::aiUpscale(*m_current, in, out), Phase::AIUpscale);
}

void ConversionEngine::onUpscaleTick()
{
    if (m_phase != Phase::AIUpscale || !m_current)
        return;
    const int done = QDir(QDir(m_work).filePath("out")).entryList(QDir::Files).size();
    const double within = qBound(0.0, double(done) / m_segFrames, 1.0);
    setProgress(qMin(0.98, (m_seg + 0.08 + within * 0.82) / m_segCount * 0.98));
}

void ConversionEngine::aiStartEncode()
{
    const QString seg = QDir(m_work).filePath(QStringLiteral("seg_%1.mkv").arg(m_seg, 5, 10, QLatin1Char('0')));
    m_current->message = QStringLiteral("Апскейл: часть %1 из %2 — сжатие").arg(m_seg + 1).arg(m_segCount);
    emit taskChanged(m_current->id);
    run(Tools::ffmpeg(),
        FfmpegCommand::aiEncodeSegment(*m_current, QDir(QDir(m_work).filePath("out")).filePath("%08d.jpg"), seg, m_hw),
        Phase::AIEncode);
}

void ConversionEngine::aiStartMux()
{
    const QString list = QDir(m_work).filePath("list.txt");
    QFile f(list);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream ts(&f);
        ts.setCodec("UTF-8");
        for (int i = 0; i < m_segCount; ++i) {
            QString p = QDir(m_work).filePath(QStringLiteral("seg_%1.mkv").arg(i, 5, 10, QLatin1Char('0')));
            p.replace('\'', QStringLiteral("'\\''"));
            ts << "file '" << p << "'\n";
        }
    }
    m_phaseDuration = m_current->effectiveDuration();
    m_current->message = QStringLiteral("Сборка итогового файла…");
    emit taskChanged(m_current->id);
    run(Tools::ffmpeg(), FfmpegCommand::aiFinalMux(*m_current, list, m_current->outputPath, m_hw, m_tempDir), Phase::AIMux);
}

void ConversionEngine::startMerge()
{
    const QString list = QDir(m_tempDir).filePath("merge_list.txt");
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
    const QString out = uniquePath(QDir(m_outDir).filePath(
        QStringLiteral("Объединённое видео %1.%2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh-mm"), ext)));
    m_outputs << out;
    emit message(QStringLiteral("Объединение видео…"));
    run(Tools::ffmpeg(), FfmpegCommand::mergeFiles(list, out), Phase::Merge);
}

QString ConversionEngine::uniquePath(const QString &path) const
{
    if (!QFile::exists(path))
        return path;
    QFileInfo fi(path);
    for (int i = 1; i < 10000; ++i) {
        const QString p = fi.dir().filePath(QStringLiteral("%1 (%2).%3").arg(fi.completeBaseName()).arg(i).arg(fi.suffix()));
        if (!QFile::exists(p))
            return p;
    }
    return path;
}
