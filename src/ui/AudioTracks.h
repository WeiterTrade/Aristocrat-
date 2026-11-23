#pragma once
// Звуковые дорожки других версий фильма (Dorcel и т. п.: английская, французская, испанская…).
// Ссылка на другую версию → скачивается только звук → вшивается в MKV релиза дорожкой с русским названием языка.
#include "core/Release.h"
#include <QObject>
#include <QQueue>
#include <functional>

#include "Frameless.h"

class QVBoxLayout;

class AudioMuxer : public QObject
{
    Q_OBJECT
public:
    static AudioMuxer *instance();
    // Звук скачан — вшить, как только MKV релиза готов (видео может ещё качаться или обрабатываться)
    void add(const QString &audioFile, const Release::AudioPending &a);

signals:
    void message(const QString &text);

private:
    explicit AudioMuxer(QObject *parent = nullptr);
    struct Job {
        QString file;
        Release::AudioPending a;
        int waits = 0;
        qint64 lastSize = -1;
    };
    void next();
    void run(Job j);

    QQueue<Job> m_queue;
    bool m_busy = false;
};

// Окно «Звуковые дорожки»: ссылки на версии фильма на других языках
class AudioTracksDialog : public FramedDialog
{
    Q_OBJECT
public:
    // dir/base — папка релиза и имя MKV без расширения; request — скачать звук по ссылке
    AudioTracksDialog(const QString &dir, const QString &base, std::function<void(const QString &)> request, QWidget *parent = nullptr);

private:
    void addRow();
    QVBoxLayout *m_rows;
    QString m_dir, m_base;
    std::function<void(const QString &)> m_request;
};
