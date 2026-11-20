#include "LocalServer.h"

#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>

LocalServer::LocalServer(QObject *parent)
    : QObject(parent), m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *s = m_server->nextPendingConnection()) {
            connect(s, &QTcpSocket::readyRead, this, [this, s] { onReadyRead(s); });
            connect(s, &QTcpSocket::disconnected, this, [this, s] {
                m_buf.remove(s);
                s->deleteLater();
            });
        }
    });
}

bool LocalServer::start()
{
    for (quint16 p : ports()) {
        if (m_server->listen(QHostAddress::LocalHost, p)) {
            m_port = p;
            return true;
        }
    }
    return false;
}

static bool extensionOrigin(const QByteArray &origin)
{
    return origin.startsWith("chrome-extension://") || origin.startsWith("moz-extension://")
        || origin.startsWith("extension://") || origin.startsWith("safari-web-extension://");
}

void LocalServer::reply(QTcpSocket *s, int code, const QByteArray &body, const QByteArray &origin)
{
    QByteArray r = "HTTP/1.1 " + QByteArray::number(code) + (code == 200 ? " OK" : code == 204 ? " No Content" : " Forbidden") + "\r\n";
    r += "Content-Type: application/json; charset=utf-8\r\n";
    if (extensionOrigin(origin)) {
        r += "Access-Control-Allow-Origin: " + origin + "\r\n";
        r += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
        r += "Access-Control-Allow-Headers: content-type\r\n";
        r += "Access-Control-Allow-Private-Network: true\r\n";
    }
    r += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    s->write(r);
    s->disconnectFromHost();
}

void LocalServer::onReadyRead(QTcpSocket *s)
{
    QByteArray &buf = m_buf[s];
    buf += s->readAll();
    if (buf.size() > 4 * 1024 * 1024) {   // слишком большой запрос
        s->abort();
        return;
    }
    const int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return;
    const QList<QByteArray> lines = buf.left(headerEnd).split('\n');
    if (lines.isEmpty())
        return;
    const QList<QByteArray> req = lines.first().trimmed().split(' ');
    const QByteArray method = req.value(0), path = req.value(1);
    QByteArray origin;
    int length = 0;
    for (const QByteArray &l : lines.mid(1)) {
        const int c = l.indexOf(':');
        if (c < 0) continue;
        const QByteArray name = l.left(c).trimmed().toLower(), value = l.mid(c + 1).trimmed();
        if (name == "origin") origin = value;
        else if (name == "content-length") length = value.toInt();
    }
    if (buf.size() < headerEnd + 4 + length)
        return;   // тело ещё не пришло
    const QByteArray body = buf.mid(headerEnd + 4, length);
    m_buf.remove(s);

    // Запросы со страниц сайтов (Origin: http/https/null) отклоняем — принимаем только расширение браузера
    if (!origin.isEmpty() && !extensionOrigin(origin)) {
        reply(s, 403, "{\"error\":\"forbidden\"}", origin);
        return;
    }
    if (method == "OPTIONS") {
        reply(s, 204, QByteArray(), origin);
    } else if (method == "GET" && path.startsWith("/ping")) {
        reply(s, 200, "{\"app\":\"aristocrat\",\"version\":\"" APP_VERSION "\"}", origin);
    } else if (method == "POST" && path.startsWith("/add")) {
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject() || doc.object().value("url").toString().isEmpty()) {
            reply(s, 403, "{\"error\":\"bad request\"}", origin);
            return;
        }
        emit mediaReceived(doc.object());
        reply(s, 200, "{\"ok\":true}", origin);
    } else {
        reply(s, 403, "{\"error\":\"unknown\"}", origin);
    }
}
