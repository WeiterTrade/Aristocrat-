#pragma once
#include "core/Chapters.h"

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <functional>

class QWebEngineView;
class QWebEnginePage;

// Скрипты для страниц: поиск сцен (возвращает JSON-строку) и подтверждение 18+
const char *sceneScript();
const char *ageScript();

// Чтение списка сцен со страницы фильма (Adult Empire и похожие сайты):
// название сцены и тайм-коды «00:35:37 - 01:22:37».
class SceneFetcher : public QObject
{
    Q_OBJECT
public:
    explicit SceneFetcher(QObject *parent = nullptr);
    ~SceneFetcher() override;
    void start(const QUrl &page);

signals:
    void progress(const QString &text);
    void finished(const QList<Chapter> &scenes);
    void failed(const QString &reason);

private:
    void onLoad();
    void extract();

    QWebEngineView *m_view;
    QTimer m_timeout;
    QList<QUrl> m_urls;      // адреса для попыток: страница фильма, затем страница с плеером
    int m_tries = 0;
    bool m_done = false;
};

// Загрузка файла по ссылке через сетевой стек WebEngine (HTTPS без OpenSSL).
// Запросы выполняются по очереди; при ошибке в обратный вызов приходят пустые данные.
class WebFetch
{
public:
    using Callback = std::function<void(const QByteArray &data)>;
    static void get(const QUrl &url, QObject *context, Callback cb);
};

// Перевод строк на русский через бесплатный сервис Google Переводчик (без ключа).
class Translator : public QObject
{
    Q_OBJECT
public:
    explicit Translator(QObject *parent = nullptr);
    ~Translator() override;
    void translate(const QStringList &lines, const QString &to = QStringLiteral("ru"));

signals:
    void finished(const QStringList &lines);
    void failed(const QString &reason);

private:
    void next();
    QStringList m_in, m_out;
    QString m_to;
    int m_index = 0;
    int m_attempt = 0;   // повторы при сбое: другие адреса сервиса
    bool m_oneByOne = false;
};
