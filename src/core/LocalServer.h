#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>

class QTcpServer;
class QTcpSocket;

// Маленький HTTP-сервер на 127.0.0.1 для связи с расширением браузера «Aristocrat — захват видео».
// Принимает запросы только от расширений браузера (заголовок Origin: chrome-extension:// или moz-extension://),
// поэтому обычные сайты не могут ничего прислать в программу.
class LocalServer : public QObject
{
    Q_OBJECT
public:
    explicit LocalServer(QObject *parent = nullptr);
    bool start();
    quint16 port() const { return m_port; }
    static QList<quint16> ports() { return {47615, 47616, 47617, 47618, 47619}; }

signals:
    void mediaReceived(const QJsonObject &media);

private:
    void onReadyRead(QTcpSocket *s);
    void reply(QTcpSocket *s, int code, const QByteArray &body, const QByteArray &origin);

    QTcpServer *m_server;
    QHash<QTcpSocket *, QByteArray> m_buf;
    quint16 m_port = 0;
};
