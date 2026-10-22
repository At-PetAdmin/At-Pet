#include "httpapi.h"

#include "agentmonitor.h"
#include "agentregistry.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>

HttpApi::HttpApi(std::function<QJsonObject()> stateProvider, QObject *parent)
    : QObject(parent), stateProvider_(std::move(stateProvider)) {
    connect(&server_, &QTcpServer::newConnection, this, &HttpApi::acceptConnections);
}

bool HttpApi::start(quint16 port) {
    return server_.listen(QHostAddress::LocalHost, port);
}

quint16 HttpApi::port() const { return server_.serverPort(); }

void HttpApi::resolvePermission(const QString &requestId, bool allowOnce) {
    auto it = permissionRequests_.find(requestId);
    if (it == permissionRequests_.end() || it->decision != "pending")
        return;

    it->decision = allowOnce ? QStringLiteral("allowed_once") : QStringLiteral("denied");
    if (!it->isClaudeHttpHook)
        return;

    const QPointer<QTcpSocket> hookSocket = it->hookSocket;
    const AgentMonitorStrategy *strategy = agentMonitorStrategy(AgentMonitorType::ClaudeCodeHttpHooks);
    const QJsonObject response = strategy ? strategy->permissionResponse(allowOnce) : QJsonObject{};
    permissionRequests_.erase(it);
    if (hookSocket) {
        permissionHookRequests_.remove(hookSocket.data());
        sendJson(hookSocket, 200, response);
    }
}

void HttpApi::acceptConnections() {
    while (server_.hasPendingConnections()) {
        QTcpSocket *socket = server_.nextPendingConnection();
        requestBuffers_.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readRequest(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            eventClients_.removeAll(socket);
            requestBuffers_.remove(socket);
            const QString requestId = permissionHookRequests_.take(socket);
            if (!requestId.isEmpty()) {
                permissionRequests_.remove(requestId);
                emit permissionRequestCancelled(requestId);
            }
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
    const QByteArray permissionPrefix = "/permission/";
    if (method == "GET" && path.startsWith(permissionPrefix)) {
        const QString requestId = QString::fromUtf8(path.mid(permissionPrefix.size()));
        auto it = permissionRequests_.find(requestId);
        if (it == permissionRequests_.end()) {
            sendJson(socket, 404, {{"error", "permission request not found"}});
            return;
        }
        const QString decision = it->decision;
        if (decision != "pending")
            permissionRequests_.erase(it);
        sendJson(socket, 200, {{"requestId", requestId}, {"status", decision}});
        return;
    }
    if (method == "DELETE" && path.startsWith(permissionPrefix)) {
        const QString requestId = QString::fromUtf8(path.mid(permissionPrefix.size()));
        auto it = permissionRequests_.find(requestId);
        if (it == permissionRequests_.end()) {
            sendJson(socket, 404, {{"error", "permission request not found"}});
            return;
        }
        if (it->isClaudeHttpHook) {
            sendJson(socket, 409, {{"error", "Claude hook requests cannot be cancelled here"}});
            return;
        }
        const bool wasPending = it->decision == "pending";
        permissionRequests_.erase(it);
        if (wasPending)
            emit permissionRequestCancelled(requestId);
        sendJson(socket, 202, {{"cancelled", wasPending}});
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
    const QByteArray nativeAgentPrefix = "/agent-event/";
    if (path.startsWith(nativeAgentPrefix)) {
        const QString agentKey = QString::fromUtf8(path.mid(nativeAgentPrefix.size()));
        const AgentDefinition *agent = agentDefinitionFor(agentKey);
        if (!agent) {
            sendJson(socket, 404, {{"error", "unknown agent adapter"}});
            return;
        }
        const AgentMonitorStrategy *strategy = agentMonitorStrategy(agent->monitorType);
        if (!strategy || !strategy->acceptsNativeEvents()) {
            sendJson(socket, 404, {{"error", "agent has no native event adapter"}});
            return;
        }
        if (strategy->isPermissionRequest(object)) {
            QJsonObject request;
            QString error;
            if (!strategy->normalizePermissionRequest(object, &request, &error)) {
                sendJson(socket, 400, {{"error", error}});
                return;
            }
            const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            request.insert("requestId", requestId);
            PermissionRequest pending;
            pending.details = request;
            pending.isClaudeHttpHook = strategy->usesNativeHookResponse();
            pending.hookSocket = socket;
            permissionRequests_.insert(requestId, pending);
            if (pending.isClaudeHttpHook)
                permissionHookRequests_.insert(socket, requestId);
            emit permissionRequestReceived(requestId, request);
            return;
        }

        QJsonObject normalizedEvent;
        QString error;
        if (!strategy->normalizeEvent(object, &normalizedEvent, &error)) {
            sendJson(socket, 400, {{"error", error}});
            return;
        }
        if (!normalizedEvent.isEmpty())
            emit eventReceived(normalizedEvent);
        if (strategy->usesNativeHookResponse())
            sendJson(socket, 200, {});
        else
            sendJson(socket, 202, {{"accepted", true}});
        return;
    }
    if (path == "/permission") {
        const QString sessionId = object.value("sessionId").toString().trimmed();
        const QString agentApp = object.value("agentApp").toString().trimmed();
        QString tool = object.value("toolName").toString().trimmed();
        if (tool.isEmpty())
            tool = object.value("tool").toString().trimmed();
        if (tool.isEmpty())
            tool = object.value("permission").toString().trimmed();
        if (sessionId.isEmpty() || agentApp.isEmpty() || tool.isEmpty()) {
            sendJson(socket, 400, {{"error", "sessionId, agentApp, and toolName are required"}});
            return;
        }
        const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject request = object;
        request.insert("requestId", requestId);
        request.insert("sessionId", sessionId.left(256));
        request.insert("agentApp", agentApp.left(64));
        request.insert("toolName", tool.left(160));
        request.insert("title", object.value("title").toString().left(160));
        request.insert("description", object.value("description").toString().left(2000));
        permissionRequests_.insert(requestId, {request, QStringLiteral("pending")});
        emit permissionRequestReceived(requestId, request);
        sendJson(socket, 202, {{"accepted", true}, {"requestId", requestId}, {"status", "pending"}});
        return;
    }
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
    if (status == 409) reason = "Conflict";
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
