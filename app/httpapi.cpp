#include "httpapi.h"

#include "agentmonitor.h"
#include "agentregistry.h"
#include "petid.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>

namespace {

// The API is unauthenticated by design, so the only thing separating a web page from this
// process is what the request itself proves about its origin. Browsers cannot forge either
// of these headers:
//   * Host is the URL the page was loaded from. During a DNS-rebinding attack that is the
//     attacker's domain, never a loopback name.
//   * Origin is attached to every cross-origin (and form) POST, including the "simple"
//     text/plain requests that skip the CORS preflight.
// Trusted local clients (hooks, bridges, curl, PowerShell, Node fetch) send neither a
// browser Origin nor a foreign Host.
bool isLoopbackHostValue(const QByteArray &rawValue, quint16 serverPort) {
    const QByteArray value = rawValue.trimmed().toLower();
    if (value.isEmpty())
        return false;

    QByteArray name;
    QByteArray portText;
    bool hasPort = false;
    if (value.startsWith('[')) {
        const qsizetype end = value.indexOf(']');
        if (end < 0)
            return false;
        name = value.left(end + 1);
        if (end + 1 < value.size()) {
            if (value.at(end + 1) != ':')
                return false;
            hasPort = true;
            portText = value.mid(end + 2);
        }
    } else {
        const qsizetype colon = value.indexOf(':');
        hasPort = colon >= 0;
        name = hasPort ? value.left(colon) : value;
        if (hasPort)
            portText = value.mid(colon + 1);
    }

    if (name != "127.0.0.1" && name != "localhost" && name != "[::1]")
        return false;
    if (!hasPort)
        return true; // Port 80 omits it; every other loopback port is explicit.
    bool ok = false;
    const uint port = portText.toUInt(&ok);
    return ok && port == static_cast<uint>(serverPort);
}

bool isJsonContentType(const QByteArray &rawValue) {
    const QByteArray value = rawValue.trimmed().toLower();
    if (value.isEmpty())
        return true; // Older local clients omit it; browsers always send one.
    return value.startsWith("application/json");
}

} // namespace

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

void HttpApi::resolveQuestion(const QString &requestId, bool answered, const QString &answer) {
    auto it = questionRequests_.find(requestId);
    if (it == questionRequests_.end() || it->status != "pending")
        return;
    it->status = answered ? QStringLiteral("answered") : QStringLiteral("cancelled");
    it->answer = answered ? answer.left(4000) : QString();
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
    const QByteArray headerBlock = buffer.left(headerEnd);
    const QList<QByteArray> lines = headerBlock.split('\n');
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
    QHash<QByteArray, QByteArray> headers;
    bool ambiguousHeaders = false;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        if (line.isEmpty())
            continue;
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QByteArray name = line.left(colon).trimmed().toLower();
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (name == "content-length") {
            bool ok = false;
            contentLength = value.toLongLong(&ok);
            if (!ok || contentLength < 0 || contentLength > 1024 * 1024) {
                sendJson(socket, 400, {{"error", "invalid content length"}});
                return;
            }
        }
        // A smuggled second copy must not be able to decide the checks below.
        if (headers.contains(name) &&
            (name == "host" || name == "origin" || name == "content-type"))
            ambiguousHeaders = true;
        headers.insert(name, value);
    }
    const qsizetype bodyStart = headerEnd + 4;
    if (buffer.size() - bodyStart < contentLength)
        return;

    const QByteArray body = buffer.mid(bodyStart, contentLength);
    const QByteArray method = requestLine.at(0).toUpper();
    const QByteArray path = requestLine.at(1).split('?').first();
    requestBuffers_.remove(socket);
    if (!requestIsTrusted(socket, method, headers, ambiguousHeaders))
        return;
    routeRequest(socket, method, path, body);
}

bool HttpApi::requestIsTrusted(QTcpSocket *socket, const QByteArray &method,
                               const QHash<QByteArray, QByteArray> &headers,
                               bool ambiguousHeaders) {
    if (ambiguousHeaders) {
        sendJson(socket, 403, {{"error", "duplicate Host, Origin, or Content-Type header"}});
        return false;
    }
    // Browsers attach this to every cross-site (and form) POST, so rejecting any Origin
    // closes CSRF without touching the raw clients that integrate with the API.
    if (headers.contains("origin")) {
        sendJson(socket, 403, {{"error", "requests with an Origin header are not allowed"}});
        return false;
    }
    if (!isLoopbackHostValue(headers.value("host"), server_.serverPort())) {
        sendJson(socket, 403, {{"error", "Host must be 127.0.0.1, localhost, or [::1]"}});
        return false;
    }
    // A browser cannot send a JSON Content-Type without a CORS preflight, and Origin above
    // already rejects that preflight. This keeps "simple" text/plain and form posts out even
    // if a client someday omits Origin.
    if (method == "POST" && !isJsonContentType(headers.value("content-type"))) {
        sendJson(socket, 415, {{"error", "Content-Type must be application/json"}});
        return false;
    }
    return true;
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
    const QByteArray questionPrefix = "/question/";
    if (method == "GET" && path.startsWith(questionPrefix)) {
        const QString requestId = QString::fromUtf8(path.mid(questionPrefix.size()));
        auto it = questionRequests_.find(requestId);
        if (it == questionRequests_.end()) {
            sendJson(socket, 404, {{"error", "question request not found"}});
            return;
        }
        const QString status = it->status;
        const QString answer = it->answer;
        if (status != "pending")
            questionRequests_.erase(it);
        QJsonObject response{{"requestId", requestId}, {"status", status}};
        if (status == "answered")
            response.insert("answer", answer);
        sendJson(socket, 200, response);
        return;
    }
    if (method == "DELETE" && path.startsWith(questionPrefix)) {
        const QString requestId = QString::fromUtf8(path.mid(questionPrefix.size()));
        auto it = questionRequests_.find(requestId);
        if (it == questionRequests_.end()) {
            sendJson(socket, 404, {{"error", "question request not found"}});
            return;
        }
        const bool wasPending = it->status == "pending";
        questionRequests_.erase(it);
        if (wasPending)
            emit questionRequestCancelled(requestId);
        sendJson(socket, 202, {{"cancelled", wasPending}});
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
        // No Access-Control-Allow-Origin: a cross-origin page (including a sandboxed
        // iframe with a null origin) must not be able to read this stream.
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n");
        eventClients_.append(socket);
        const QByteArray payload = QJsonDocument(stateProvider_()).toJson(QJsonDocument::Compact);
        socket->write("data: " + payload + "\n\n");
        socket->flush();
        return;
    }
    if (method == "OPTIONS") {
        // Deliberately no CORS headers: browser preflights must never succeed. The Origin
        // check in requestIsTrusted() rejects the preflight that browsers send anyway.
        socket->write("HTTP/1.1 204 No Content\r\nAllow: GET, POST, OPTIONS\r\nConnection: close\r\n\r\n");
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
    if (path == "/question") {
        const QString agentApp = object.value("agentApp").toString().trimmed();
        const QString question = object.value("question").toString().trimmed();
        if (agentApp.isEmpty() || question.isEmpty()) {
            sendJson(socket, 400, {{"error", "agentApp and question are required"}});
            return;
        }
        const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject request{{"requestId", requestId}, {"agentApp", agentApp.left(64)},
                            {"question", question.left(2000)}};
        request.insert("sessionId", object.value("sessionId").toString().trimmed().left(256));
        request.insert("header", object.value("header").toString().left(160));
        QJsonArray choices;
        const QJsonArray inputChoices = object.value("choices").toArray();
        for (qsizetype i = 0; i < inputChoices.size() && i < 20; ++i) {
            const QString choice = inputChoices.at(i).toString().trimmed();
            if (!choice.isEmpty())
                choices.append(choice.left(200));
        }
        if (!choices.isEmpty())
            request.insert("choices", choices);
        questionRequests_.insert(requestId, {request});
        emit questionRequestReceived(requestId, request);
        sendJson(socket, 202, {{"accepted", true}, {"requestId", requestId}, {"status", "pending"}});
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
        // petId names a directory in the pet library; it must never become a path
        // (see petid.h and PetController::switchPet()).
        if (!object.value("all").toBool() && !petid::isSafe(object.value("petId").toString())) {
            sendJson(socket, 400, {{"error", "petId must be a plain pet library id"}});
            return;
        }
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
    if (status == 403) reason = "Forbidden";
    if (status == 404) reason = "Not Found";
    if (status == 405) reason = "Method Not Allowed";
    if (status == 409) reason = "Conflict";
    if (status == 413) reason = "Payload Too Large";
    if (status == 415) reason = "Unsupported Media Type";
    // Deliberately no Access-Control-Allow-Origin. Emitting one (even "null") lets an
    // iframe-sandboxed page read these responses.
    const QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " " + reason +
        "\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: " +
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
