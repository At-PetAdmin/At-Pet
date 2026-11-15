// Regression tests for the trust boundary of the local HTTP API.
//
// Bearer credentials identify local clients; browser checks remain defense in depth:
//   * CSRF: a cross-site POST (even "simple" text/plain, which skips the preflight) can
//     forge /say, /event, /permission or /question. Browsers attach Origin to those
//     requests, so any Origin is rejected while raw local clients keep working.
//   * DNS rebinding: after a rebind the page is same-origin with 127.0.0.1 and could read
//     /state and /events, so the Host header must name this loopback server.
//   * Leaks: Access-Control-Allow-Origin must never be sent; "null" lets a sandboxed iframe
//     read responses.
//   * /summon forwards petId to QDir::filePath(), so traversal must be a 400.
//
// Raw sockets keep the test independent of any HTTP client's header defaults.

#include "httpapi.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>
#include <QTemporaryDir>
#include <QFile>

namespace {

constexpr int kConnectTimeoutMs = 3000;

// Reads until the server closes the connection or nothing arrives for settleMs. The SSE
// endpoint keeps the socket open, so it passes a short settle window of its own.
QByteArray exchange(quint16 port, const QByteArray &request, int settleMs = 2000) {
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    if (!socket.waitForConnected(kConnectTimeoutMs))
        return {};

    socket.write(request);
    socket.flush();

    // The server lives in this thread and only makes progress when the event loop runs.
    // QTcpSocket::waitForReadyRead() polls the client socket natively on Windows, so it
    // would never deliver newConnection/readyRead to the server; spin events instead.
    QByteArray response;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < settleMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        response += socket.readAll();
        if (socket.state() == QAbstractSocket::UnconnectedState)
            break;
        QTest::qWait(2);
    }
    response += socket.readAll();
    socket.abort();
    return response;
}

int statusOf(const QByteArray &response) {
    if (!response.startsWith("HTTP/1.1 "))
        return -1;
    return response.mid(9, 3).toInt();
}

QByteArray headersOf(const QByteArray &response) {
    const qsizetype end = response.indexOf("\r\n\r\n");
    return end < 0 ? response : response.left(end);
}

QByteArray bodyOf(const QByteArray &response) {
    const qsizetype end = response.indexOf("\r\n\r\n");
    return end < 0 ? QByteArray() : response.mid(end + 4);
}

bool hasCorsHeader(const QByteArray &response) {
    return headersOf(response).toLower().contains("access-control-");
}

QByteArray asClient(QByteArray request, const QByteArray &token) {
    return request.replace("Authorization: Bearer test-admin\r\n",
        token.isEmpty() ? QByteArray() : "Authorization: Bearer " + token + "\r\n");
}

QByteArray hostFor(quint16 port) {
    return "127.0.0.1:" + QByteArray::number(port);
}

QByteArray getRequest(const QByteArray &path, const QByteArray &host,
                      const QByteArray &extraHeaders = {}) {
    return "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nAuthorization: Bearer test-admin\r\n" + extraHeaders + "\r\n";
}

QByteArray postRequest(const QByteArray &path, const QByteArray &host, const QByteArray &body,
                       const QByteArray &contentType = "application/json",
                       const QByteArray &extraHeaders = {}) {
    QByteArray request = "POST " + path + " HTTP/1.1\r\nHost: " + host + "\r\nAuthorization: Bearer test-admin\r\n";
    if (!contentType.isEmpty())
        request += "Content-Type: " + contentType + "\r\n";
    request += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    request += extraHeaders;
    request += "\r\n";
    request += body;
    return request;
}

} // namespace

class HttpApiGuardTest final : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void requiresAuthentication();
    void bindsAgentIdentityAndRequestOwnership();
    void boundsPendingRequests();
    void persistsCredentialsAndRejectsCorruption();

    void permissionForwardingSwitch();
    void acceptsLoopbackHosts();
    void rejectsForeignHost();
    void rejectsBrowserOrigins();
    void rejectsNonJsonPostContentType();
    void neverAdvertisesCors();
    void rejectsSummonPathTraversal();
    void keepsServingTrustedClients();
    void hidesQuestionAnswersFromBrowsers();

private:
    QByteArray send(const QByteArray &request, int settleMs = 2000) {
        return exchange(port_, request, settleMs);
    }

    HttpApi *api_ = nullptr;
    quint16 port_ = 0;
};

void HttpApiGuardTest::init() {
    api_ = new HttpApi([] {
        return QJsonObject{{"app", "At-Pet"}, {"pets", QJsonArray{}}};
    });
    api_->setClients({{"admin", {}, "test-admin", true},
                      {"a", "claudecode", "test-a", false},
                      {"b", "claudecode", "test-b", false},
                      {"other", "opencode", "test-other", false}});
    QVERIFY(api_->start(0));
    port_ = api_->port();
    QVERIFY(port_ != 0);
}

void HttpApiGuardTest::cleanup() {
    delete api_;
    api_ = nullptr;
    port_ = 0;
}

void HttpApiGuardTest::requiresAuthentication() {
    const auto host = hostFor(port_);
    QCOMPARE(statusOf(send(asClient(getRequest("/health", host), {}))), 200);
    for (const auto &path : {"/state", "/events", "/question/not-owned", "/permission/not-owned"})
        QCOMPARE(statusOf(send(asClient(getRequest(path, host), {}))), 401);
    QCOMPARE(statusOf(send(asClient(postRequest("/event", host, R"({"sessionId":"s","event":"thinking"})"), {}))), 401);
    QCOMPARE(statusOf(send(asClient(getRequest("/state", host), "wrong"))), 401);
    QCOMPARE(statusOf(send(getRequest("/state", host, "Authorization: Bearer test-a\r\n"))), 403);
    QCOMPARE(statusOf(send(asClient(getRequest("/state", host), "test-a"))), 403);
    QCOMPARE(statusOf(send(asClient(getRequest("/events", host), "test-a"))), 403);
    QCOMPARE(statusOf(send(asClient(postRequest("/say", host, R"({"text":"fake"})"), "test-a"))), 403);
}

void HttpApiGuardTest::bindsAgentIdentityAndRequestOwnership() {
    const auto host = hostFor(port_);
    QSignalSpy events(api_, &HttpApi::eventReceived);
    QCOMPARE(statusOf(send(asClient(postRequest("/event", host,
        R"({"sessionId":"s","agentApp":"OpenCode","event":"thinking"})"), "test-a"))), 403);
    QCOMPARE(statusOf(send(asClient(postRequest("/agent-event/opencode", host,
        R"({"sessionID":"s","sourceEvent":"session.created"})"), "test-a"))), 403);
    QCOMPARE(statusOf(send(asClient(postRequest("/event", host,
        R"({"sessionId":"s","agentApp":"claude-code","event":"thinking","clientId":"b"})"), "test-a"))), 202);
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.first().first().toJsonObject().value("clientId").toString(), QString("a"));
    const auto accepted = send(asClient(postRequest("/question", host,
        R"({"sessionId":"s","agentApp":"Claude Code","question":"Choose?"})"), "test-a"));
    QCOMPARE(statusOf(accepted), 202);
    const auto id = QJsonDocument::fromJson(bodyOf(accepted)).object().value("requestId").toString();
    const QByteArray path = "/question/" + id.toUtf8();
    QCOMPARE(statusOf(send(asClient(getRequest(path, host), "test-b"))), 404);
    auto deletion = asClient(getRequest(path, host), "test-b");
    deletion.replace("GET ", "DELETE ");
    QCOMPARE(statusOf(send(deletion)), 404);
    api_->resolveQuestion(id, true, "private answer");
    QCOMPARE(statusOf(send(asClient(getRequest(path, host), "test-b"))), 404);
    const auto answer = send(asClient(getRequest(path, host), "test-a"));
    QVERIFY(answer.contains("private answer"));
    QCOMPARE(statusOf(send(asClient(getRequest(path, host), "test-a"))), 404);

    const auto permission = send(asClient(postRequest("/permission", host,
        R"({"sessionId":"s","agentApp":"claude-code","toolName":"Read"})"), "test-a"));
    QCOMPARE(statusOf(permission), 202);
    const auto permissionId = QJsonDocument::fromJson(bodyOf(permission)).object().value("requestId").toString();
    const QByteArray permissionPath = "/permission/" + permissionId.toUtf8();
    QCOMPARE(statusOf(send(asClient(getRequest(permissionPath, host), "test-b"))), 404);
    deletion = asClient(getRequest(permissionPath, host), "test-b");
    deletion.replace("GET ", "DELETE ");
    QCOMPARE(statusOf(send(deletion)), 404);
    api_->resolvePermission(permissionId, true);
    QVERIFY(send(asClient(getRequest(permissionPath, host), "test-a")).contains("allowed_once"));
    QCOMPARE(statusOf(send(asClient(getRequest(permissionPath, host), "test-a"))), 404);
}

void HttpApiGuardTest::boundsPendingRequests() {
    const auto request = asClient(postRequest("/question", hostFor(port_),
        R"({"agentApp":"claude-code","question":"test"})"), "test-a");
    QString firstId;
    for (int i = 0; i < 16; ++i) {
        const auto response = send(request);
        QCOMPARE(statusOf(response), 202);
        if (i == 0) firstId = QJsonDocument::fromJson(bodyOf(response)).object().value("requestId").toString();
    }
    QCOMPARE(statusOf(send(request)), 429);
    api_->resolveQuestion(firstId, false, {});
    QCOMPARE(statusOf(send(asClient(getRequest("/question/" + firstId.toUtf8(), hostFor(port_)), "test-a"))), 200);
    QCOMPARE(statusOf(send(request)), 202);
}

void HttpApiGuardTest::persistsCredentialsAndRejectsCorruption() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath("api-security/clients.json");
    QList<ApiClient> first, second;
    QString error;
    QVERIFY2(apicredentials::loadOrCreate(&first, &error, path), qPrintable(error));
    QVERIFY(first.size() > 2);
    QVERIFY2(apicredentials::loadOrCreate(&second, &error, path), qPrintable(error));
    QCOMPARE(first.size(), second.size());
    for (int i = 0; i < first.size(); ++i) {
        QVERIFY(first[i].token == second[i].token);
        QVERIFY(first[i].token.size() >= 32);
    }
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("invalid synthetic store");
    file.close();
    QVERIFY(!apicredentials::loadOrCreate(&second, &error, path));
    QVERIFY(second.isEmpty());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("invalid synthetic store"));
}

// The documented clients (hooks, bridges, curl, PowerShell) talk to 127.0.0.1 or localhost.
void HttpApiGuardTest::acceptsLoopbackHosts() {
    const QByteArray portText = QByteArray::number(port_);
    const QList<QByteArray> hosts = {
        "127.0.0.1:" + portText,
        "localhost:" + portText,
        "LOCALHOST:" + portText,
        "[::1]:" + portText,
        "127.0.0.1", // Port 80 omits it; a browser cannot produce this for another port.
    };
    for (const QByteArray &host : hosts) {
        const QByteArray response = send(getRequest("/health", host));
        QCOMPARE(statusOf(response), 200);
        QVERIFY(response.contains("\"ok\":true"));
    }
}

// A rebound page presents its own domain in Host; HTTP/1.1 also requires the header, so a
// browser can never produce an accepted value by accident.
void HttpApiGuardTest::rejectsForeignHost() {
    const QByteArray portText = QByteArray::number(port_);
    const QList<QByteArray> hosts = {
        "evil.example:" + portText,
        "127.0.0.1.evil.example:" + portText,
        "attacker.test",
        "127.0.0.1:1",
    };
    for (const QByteArray &host : hosts) {
        const QByteArray response = send(getRequest("/state", host));
        QCOMPARE(statusOf(response), 403);
        QVERIFY(!response.contains("\"pets\""));
    }

    QSignalSpy speech(api_, &HttpApi::speechRequested);
    QCOMPARE(statusOf(send(postRequest("/say", "evil.example:" + portText, R"({"text":"hijacked"})"))),
             403);
    QCOMPARE(speech.count(), 0);

    QCOMPARE(statusOf(send("GET /state HTTP/1.1\r\n\r\n")), 403);

    const QByteArray duplicated = "GET /health HTTP/1.1\r\nHost: " + hostFor(port_) +
        "\r\nHost: evil.example\r\n\r\n";
    QCOMPARE(statusOf(send(duplicated)), 403);
}

// Any Origin means a browser sent the request. That covers the phishing chain (forged
// question/permission prompts) and answer theft without affecting native clients.
void HttpApiGuardTest::rejectsBrowserOrigins() {
    const QByteArray host = hostFor(port_);
    const QByteArray body = R"({"text":"csrf"})";
    QSignalSpy speech(api_, &HttpApi::speechRequested);

    // text/plain keeps the request "simple", so no preflight is needed to send it.
    QCOMPARE(statusOf(send(postRequest("/say", host, body, "text/plain",
                                       "Origin: http://evil.example\r\n"))),
             403);
    // A sandboxed iframe sends the opaque origin as "null".
    QCOMPARE(statusOf(send(postRequest("/say", host, body, "text/plain", "Origin: null\r\n"))), 403);
    QCOMPARE(statusOf(send(postRequest("/say", host, body, "text/plain", "Origin:\r\n"))), 403);
    // Even a loopback origin is not a supported browser client.
    QCOMPARE(statusOf(send(postRequest("/say", host, body, "application/json",
                                       "Origin: http://127.0.0.1:" + QByteArray::number(port_) +
                                           "\r\n"))),
             403);
    QCOMPARE(speech.count(), 0);

    QSignalSpy events(api_, &HttpApi::eventReceived);
    QCOMPARE(statusOf(send(postRequest("/event", host,
                                       R"({"sessionId":"s1","event":"thinking"})",
                                       "application/json", "Origin: http://evil.example\r\n"))),
             403);
    QCOMPARE(events.count(), 0);
}

// Browsers can only send form, multipart or text/plain bodies without a preflight.
void HttpApiGuardTest::rejectsNonJsonPostContentType() {
    const QByteArray host = hostFor(port_);
    const QByteArray body = R"({"text":"form"})";
    QSignalSpy speech(api_, &HttpApi::speechRequested);

    QCOMPARE(statusOf(send(postRequest("/say", host, body, "text/plain"))), 415);
    QCOMPARE(statusOf(send(postRequest("/say", host, body, "application/x-www-form-urlencoded"))), 415);
    QCOMPARE(statusOf(send(postRequest("/say", host, body, "multipart/form-data; boundary=x"))), 415);
    QCOMPARE(speech.count(), 0);

    QCOMPARE(statusOf(send(postRequest("/say", host, body, "application/json; charset=utf-8"))), 202);
    QCOMPARE(speech.count(), 1);
}

void HttpApiGuardTest::neverAdvertisesCors() {
    const QByteArray health = send(getRequest("/health", hostFor(port_)));
    QCOMPARE(statusOf(health), 200);
    QVERIFY(!hasCorsHeader(health));

    const QByteArray options = send("OPTIONS /say HTTP/1.1\r\nHost: " + hostFor(port_) + "\r\nAuthorization: Bearer test-admin\r\n\r\n");
    QCOMPARE(statusOf(options), 204);
    QVERIFY(!hasCorsHeader(options));

    const QByteArray stream = send(getRequest("/events", hostFor(port_)), 500);
    QCOMPARE(statusOf(stream), 200);
    QVERIFY(stream.contains("text/event-stream"));
    QVERIFY(stream.contains("data:"));
    QVERIFY(!hasCorsHeader(stream));
}

void HttpApiGuardTest::rejectsSummonPathTraversal() {
    const QByteArray host = hostFor(port_);
    QSignalSpy summon(api_, &HttpApi::summonRequested);

    // Valid JSON that decodes to a traversing id on either platform.
    const QList<QByteArray> bodies = {
        R"({"petId":"../../x"})",
        R"({"petId":"..\\..\\x"})",
        R"({"petId":".."})",
        R"({"petId":"/etc/passwd"})",
        R"({"petId":"C:\\Windows"})",
    };
    for (const QByteArray &body : bodies) {
        QCOMPARE(statusOf(send(postRequest("/summon", host, body))), 400);
    }
    QCOMPARE(summon.count(), 0);

    QCOMPARE(statusOf(send(postRequest("/summon", host, R"({"petId":"sample-cat"})"))), 202);
    QCOMPARE(summon.count(), 1);
    QCOMPARE(summon.at(0).at(0).toJsonObject().value("petId").toString(), QStringLiteral("sample-cat"));

    QCOMPARE(statusOf(send(postRequest("/summon", host, R"({"all":true})"))), 202);
    QCOMPARE(summon.count(), 2);
}

// The guard must not break the integrations the API exists for.
void HttpApiGuardTest::keepsServingTrustedClients() {
    const QByteArray host = hostFor(port_);

    const QByteArray health = send(getRequest("/health", host));
    QCOMPARE(statusOf(health), 200);
    const auto healthJson = QJsonDocument::fromJson(bodyOf(health)).object();
    QVERIFY(healthJson.value("ok").toBool());
    QVERIFY(healthJson.value("authenticationRequired").toBool());

    QCOMPARE(statusOf(send(getRequest("/state", host))), 200);

    QSignalSpy events(api_, &HttpApi::eventReceived);
    const QByteArray eventBody = R"({"sessionId":"session-1","event":"thinking","message":"ok"})";
    QCOMPARE(statusOf(send(postRequest("/event", host, eventBody))), 202);
    QCOMPARE(events.count(), 1);
    QCOMPARE(events.at(0).at(0).toJsonObject().value("sessionId").toString(), QStringLiteral("session-1"));

    QSignalSpy speech(api_, &HttpApi::speechRequested);
    QCOMPARE(statusOf(send(postRequest("/say", host, R"({"text":"hello"})"))), 202);
    QCOMPARE(speech.count(), 1);
    QCOMPARE(speech.at(0).at(0).toString(), QStringLiteral("hello"));
}

// The phishing case from the report: a forged question prompt must neither be readable nor
// answered by the page that created it, while the trusted poller keeps working.
void HttpApiGuardTest::hidesQuestionAnswersFromBrowsers() {
    const QByteArray host = hostFor(port_);
    const QByteArray created = send(postRequest(
        "/question", host, R"({"agentApp":"Claude Code","question":"Paste your API token"})"));
    QCOMPARE(statusOf(created), 202);
    const QString requestId =
        QJsonDocument::fromJson(bodyOf(created)).object().value("requestId").toString();
    QVERIFY(!requestId.isEmpty());

    api_->resolveQuestion(requestId, true, QStringLiteral("token-1234"));

    const QByteArray browserPoll =
        send(getRequest("/question/" + requestId.toUtf8(), host, "Origin: http://evil.example\r\n"));
    QCOMPARE(statusOf(browserPoll), 403);
    QVERIFY(!browserPoll.contains("token-1234"));

    const QByteArray trusted = send(getRequest("/question/" + requestId.toUtf8(), host));
    QCOMPARE(statusOf(trusted), 200);
    const QJsonObject payload = QJsonDocument::fromJson(bodyOf(trusted)).object();
    QCOMPARE(payload.value("status").toString(), QStringLiteral("answered"));
    QCOMPARE(payload.value("answer").toString(), QStringLiteral("token-1234"));
}

QTEST_GUILESS_MAIN(HttpApiGuardTest)

#include "tst_httpapiguard.moc"

void HttpApiGuardTest::permissionForwardingSwitch() {
    const auto host = "127.0.0.1:" + QByteArray::number(port_);
    QSignalSpy permissions(api_, &HttpApi::permissionRequestReceived);
    QSignalSpy events(api_, &HttpApi::eventReceived);
    api_->setPermissionForwardingEnabled(false);
    const auto generic = send(postRequest("/permission", host,
        R"({"agentApp":"opencode","sessionId":"a","toolName":"test"})"));
    QVERIFY(generic.startsWith("HTTP/1.1 409"));
    QVERIFY(generic.contains("permission_forwarding_disabled"));
    const auto native = send(postRequest("/agent-event/claudecode", host,
        R"({"hook_event_name":"PermissionRequest","session_id":"a","tool_name":"Bash","tool_input":{"command":"echo hi"}})"));
    QVERIFY(native.startsWith("HTTP/1.1 200"));
    QVERIFY(!native.contains("permissionDecision"));
    QCOMPARE(permissions.size(), 0);
    QVERIFY(send(postRequest("/event", host, R"({"sessionId":"a","event":"thinking"})")).startsWith("HTTP/1.1 202"));
    QCOMPARE(events.size(), 1);
    api_->setPermissionForwardingEnabled(true);
    const auto accepted = send(postRequest("/permission", host,
        R"({"agentApp":"opencode","sessionId":"a","toolName":"test"})"));
    QVERIFY(accepted.startsWith("HTTP/1.1 202"));
    QCOMPARE(permissions.size(), 1);
    const auto id = permissions.first().first().toString();
    api_->setPermissionForwardingEnabled(false);
    api_->resolvePermission(id, false);
    const auto result = send(getRequest("/permission/" + id.toUtf8(), host));
    QVERIFY(result.contains("denied"));
}
