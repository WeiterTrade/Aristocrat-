#pragma once
// Обложки «как в генераторе Adult Aristocrat»: программа загружает тот же HTML-скрипт (resources/covers/editor.html)
// и вызывает его функции рисования — значок «4K UHD», полоса «DVD Эксклюзив», общая обложка из нескольких частей.
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QTimer>
#include <QWidget>
#include <functional>

class QWebEnginePage;
class QWebEngineProfile;
class QWebEngineView;
class QLabel;

namespace CoverArt {

// Отдельный профиль браузера для генератора (его сохранения не путаются с остальными загрузками программы)
QWebEngineProfile *profile();

// Значки на постер (файл перезаписывается): 4K — «4K UHD» внизу слева, DVD — полоса «DVD Эксклюзив» сверху.
// done(true) — готово, done(false) — не получилось (файл не тронут).
void stampPoster(const QString &file, bool k4, bool dvd, QObject *ctx, std::function<void(bool)> done);

// Добавить в NFO жанр (если его там нет)
bool addGenre(const QString &nfoFile, const QString &genre);

} // namespace CoverArt

// Невидимая страница с генератором: очередь заданий «нарисовать значки»
class CoverEngine : public QObject
{
    Q_OBJECT
public:
    static CoverEngine *instance();
    void stamp(const QString &file, bool k4, bool dvd, QObject *ctx, std::function<void(bool)> done);

private:
    explicit CoverEngine(QObject *parent = nullptr);
    struct Job {
        QString file;
        bool k4 = false, dvd = false;
        QPointer<QObject> ctx;
        bool hasCtx = false;
        std::function<void(bool)> done;
    };
    void next();
    void poll(int tries);
    void finish(bool ok, const QByteArray &jpeg = {});

    QWebEnginePage *m_page = nullptr;
    bool m_ready = false, m_busy = false;
    QQueue<Job> m_queue;
    Job m_cur;
};

// Вкладка «Общая обложка»: раздел генератора «Общая обложка для нескольких частей» целиком
class CoverCollagePage : public QWidget
{
    Q_OBJECT
public:
    explicit CoverCollagePage(QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *e) override;

private:
    QWebEngineView *m_view = nullptr;
    QLabel *m_state = nullptr;
    bool m_loaded = false;
};
