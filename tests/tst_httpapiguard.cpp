// Regression tests for the trust boundary of the local HTTP API.
//
// README documents the API as unauthenticated and "intended only for trusted programs on
// the same machine". A web page is not one, yet it can reach the API:
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

QByteArray hostFor(quint16 port) {
    return "127.0.0.1:" + QByteArray::number(port);
}

QByteArray getRequest(const QByteArray &path, const QByteArray &host,
                      const QByteArray &extraHeaders = {}) {
    return "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\n" + extraHeaders + "\r\n";
}

QByteArray postRequest(const QByteArray &path, const QByteArray &host, const QByteArray &body,
                       const QByteArray &contentType = "application/json",
                       const QByteArray &extraHeaders = {}) {
    QByteArray request = "POST " + path + " HTTP/1.1\r\nHost: " + host + "\r\n";
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
    QVERIFY(api_->start(0));
    port_ = api_->port();
    QVERIFY(port_ != 0);
}

void HttpApiGuardTest::cleanup() {
    delete api_;
    api_ = nullptr;
    port_ = 0;
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

    const QByteArray options = send("OPTIONS /say HTTP/1.1\r\nHost: " + hostFor(port_) + "\r\n\r\n");
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
    QCOMPARE(bodyOf(health), QByteArray(R"({"ok":true})"));

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
