#pragma once
#include "core/Chapters.h"

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>

class QWebEngineView;
class QWebEnginePage;

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
    QWebEnginePage *m_page;
    QStringList m_in, m_out;
    QString m_to;
    int m_index = 0;
};
