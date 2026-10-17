#include "httpapi.h"

#include <QJsonDocument>
#include <QJsonParseError>

HttpApi::HttpApi(std::function<QJsonObject()> stateProvider, QObject *parent)
    : QObject(parent), stateProvider_(std::move(stateProvider)) {
    connect(&server_, &QTcpServer::newConnection, this, &HttpApi::acceptConnections);
}

bool HttpApi::start(quint16 port) {
    return server_.listen(QHostAddress::LocalHost, port);
}

quint16 HttpApi::port() const { return server_.serverPort(); }

void HttpApi::acceptConnections() {
    while (server_.hasPendingConnections()) {
        QTcpSocket *socket = server_.nextPendingConnection();
        requestBuffers_.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readRequest(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            eventClients_.removeAll(socket);
            requestBuffers_.remove(socket);
            socket->deleteLater();
        });
    }
}

void HttpApi::readRequest(QTcpSocket *socket) {
    if (eventClients_.contains(socket)) {
        socket->readAll();
        return;
    }
    QByteArray &buffer = requestBuffers_[socket];
    buffer += socket->readAll();
    if (buffer.size() > 1024 * 1024) {
        sendJson(socket, 413, {{"error", "request too large"}});
        return;
    }

    const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return;
    const QByteArray headers = buffer.left(headerEnd);
    const QList<QByteArray> lines = headers.split('\n');
    if (lines.isEmpty()) {
        sendJson(socket, 400, {{"error", "invalid request"}});
        return;
    }
    const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
    if (requestLine.size() < 2) {
        sendJson(socket, 400, {{"error", "invalid request line"}});
        return;
    }

    qsizetype contentLength = 0;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        if (line.toLower().startsWith("content-length:")) {
            bool ok = false;
            contentLength = line.mid(line.indexOf(':') + 1).trimmed().toLongLong(&ok);
            if (!ok || contentLength < 0 || contentLength > 1024 * 1024) {
                sendJson(socket, 400, {{"error", "invalid content length"}});
                return;
            }
        }
    }
    const qsizetype bodyStart = headerEnd + 4;
    if (buffer.size() - bodyStart < contentLength)
        return;

    const QByteArray body = buffer.mid(bodyStart, contentLength);
    const QByteArray method = requestLine.at(0).toUpper();
    const QByteArray path = requestLine.at(1).split('?').first();
    requestBuffers_.remove(socket);
    routeRequest(socket, method, path, body);
}

void HttpApi::routeRequest(QTcpSocket *socket, const QByteArray &method, const QByteArray &path,
                           const QByteArray &body) {
    if (method == "GET" && path == "/health") {
        sendJson(socket, 200, {{"ok", true}});
        return;
    }
    if (method == "GET" && path == "/state") {
        sendJson(socket, 200, stateProvider_());
        return;
    }
    if (method == "GET" && path == "/events") {
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\nAccess-Control-Allow-Origin: null\r\n\r\n");
        eventClients_.append(socket);
        const QByteArray payload = QJsonDocument(stateProvider_()).toJson(QJsonDocument::Compact);
        socket->write("data: " + payload + "\n\n");
        socket->flush();
        return;
    }
    if (method == "OPTIONS") {
        socket->write("HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: null\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\nConnection: close\r\n\r\n");
        socket->disconnectFromHost();
        return;
    }
    if (method != "POST") {
        sendJson(socket, 405, {{"error", "method not allowed"}});
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        sendJson(socket, 400, {{"error", "expected a JSON object"}});
        return;
    }
    const QJsonObject object = document.object();
    if (path == "/event") {
        if (object.value("sessionId").toString().isEmpty() || object.value("event").toString().isEmpty()) {
            sendJson(socket, 400, {{"error", "sessionId and event are required"}});
            return;
        }
        emit eventReceived(object);
        sendJson(socket, 202, {{"accepted", true}});
        return;
    }
    if (path == "/animation") {
        const QString name = object.value("name").toString();
        if (name.isEmpty()) {
            sendJson(socket, 400, {{"error", "name is required"}});
            return;
        }
        emit animationRequested(name);
        sendJson(socket, 202, {{"accepted", true}});
        return;
    }
    if (path == "/say") {
        const QString text = object.value("text").toString();
        if (text.isEmpty()) {
            sendJson(socket, 400, {{"error", "text is required"}});
            return;
        }
        emit speechRequested(text);
        sendJson(socket, 202, {{"accepted", true}});
        return;
    }
    if (path == "/summon") {
        emit summonRequested(object);
        sendJson(socket, 202, {{"accepted", true}});
        return;
    }
    sendJson(socket, 404, {{"error", "not found"}});
}

void HttpApi::sendJson(QTcpSocket *socket, int status, const QJsonObject &json) {
    const QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);
    QByteArray reason = "OK";
    if (status == 202) reason = "Accepted";
    if (status == 400) reason = "Bad Request";
    if (status == 404) reason = "Not Found";
    if (status == 405) reason = "Method Not Allowed";
    if (status == 413) reason = "Payload Too Large";
    const QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " " + reason +
        "\r\nContent-Type: application/json; charset=utf-8\r\nAccess-Control-Allow-Origin: null\r\nContent-Length: " +
        QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    socket->write(response);
    socket->disconnectFromHost();
}

void HttpApi::publishState() { broadcastState(); }

void HttpApi::broadcastState() {
    const QByteArray payload = QJsonDocument(stateProvider_()).toJson(QJsonDocument::Compact);
    const QByteArray message = "data: " + payload + "\n\n";
    for (QTcpSocket *client : eventClients_) {
        if (client->state() == QAbstractSocket::ConnectedState) {
            client->write(message);
            client->flush();
        }
    }
}
