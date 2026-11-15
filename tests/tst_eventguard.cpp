#include "agenteventguard.h"
#include <QTest>

class EventGuardTest : public QObject {
    Q_OBJECT
    static QJsonObject event(const QString &name, const QString &run, int sequence) {
        return {{"event", name}, {"runId", run}, {"sequence", sequence}};
    }
private slots:
    void rejectsLateEventsAndRetiredRuns() {
        AgentEventGuard guard;
        auto start = event("started", "run-1", 1);
        QVERIFY(guard.accept("client/session", &start));
        auto done = event("done", "run-1", 3);
        QVERIFY(guard.accept("client/session", &done));
        auto late = event("tool_start", "run-1", 2);
        QVERIFY(!guard.accept("client/session", &late));
        auto afterDone = event("tool_start", "run-1", 4);
        QVERIFY(!guard.accept("client/session", &afterDone));
        auto nextRun = event("started", "run-2", 1);
        auto lateStart = event("started", "run-1", 5);
        QVERIFY(!guard.accept("client/session", &lateStart));
        QVERIFY(guard.accept("client/session", &nextRun));
        auto replay = event("started", "run-1", 5);
        QVERIFY(!guard.accept("client/session", &replay));
        auto downgrade = QJsonObject{{"event", "tool_start"}};
        QVERIFY(!guard.accept("client/session", &downgrade));
    }
    void toolCallsRemainIndependent() {
        AgentEventGuard guard;
        auto a = event("tool_start", "run", 1); a.insert("toolCallId", "a");
        auto b = event("tool_start", "run", 2); b.insert("toolCallId", "b");
        QVERIFY(guard.accept("s", &a));
        QVERIFY(guard.accept("s", &b));
        QCOMPARE(b.value("activeToolCount").toInt(), 2);
        auto endA = event("tool_end", "run", 3); endA.insert("toolCallId", "a");
        QVERIFY(guard.accept("s", &endA));
        QCOMPARE(endA.value("event").toString(), QString("tool_running"));
        QCOMPARE(endA.value("activeToolCount").toInt(), 1);
        auto duplicate = event("tool_end", "run", 4); duplicate.insert("toolCallId", "a");
        QVERIFY(!guard.accept("s", &duplicate));
        auto endB = event("tool_end", "run", 5); endB.insert("toolCallId", "b");
        QVERIFY(guard.accept("s", &endB));
        QCOMPARE(endB.value("activeToolCount").toInt(), 0);
        auto lateStart = event("tool_start", "run", 6); lateStart.insert("toolCallId", "a");
        QVERIFY(!guard.accept("s", &lateStart));
    }
    void reorderedIndependentToolsDoNotLoseRunningCalls() {
        AgentEventGuard guard;
        auto start = event("started", "run", 0);
        QVERIFY(guard.accept("s", &start));
        auto b = event("tool_start", "run", 3); b.insert("toolCallId", "b");
        QVERIFY(guard.accept("s", &b));
        auto a = event("tool_start", "run", 1); a.insert("toolCallId", "a");
        QVERIFY(guard.accept("s", &a));
        QCOMPARE(a.value("activeToolCount").toInt(), 2);
        auto endA = event("tool_end", "run", 2); endA.insert("toolCallId", "a");
        QVERIFY(guard.accept("s", &endA));
        QCOMPARE(endA.value("activeToolCount").toInt(), 1);
        auto oldLifecycle = event("thinking", "run", 2);
        QVERIFY(!guard.accept("s", &oldLifecycle));
    }
    void completionAndDisconnectTombstonesSurviveUiCleanup() {
        AgentEventGuard guard;
        QJsonObject done{{"event", "done"}, {"eventId", "completion"}};
        QVERIFY(guard.accept("s", &done, 1000));
        QVERIFY(!guard.accept("s", &done, 7000));
        QJsonObject late{{"event", "tool_start"}};
        QVERIFY(!guard.accept("s", &late, 7000));
        QJsonObject next{{"event", "turn_start"}};
        QVERIFY(guard.accept("s", &next, 8000));
        QJsonObject close{{"event", "session_end"}};
        QVERIFY(guard.accept("s", &close, 9000));
        QVERIFY(!guard.accept("s", &late, 15000));
        QVERIFY(!guard.accept("s", &next, 15000));
    }
    void sourceTimestampAndIdsAreValidated() {
        AgentEventGuard guard;
        const qint64 now = 1800000000000LL;
        const auto time = [now](qint64 offset) { return QDateTime::fromMSecsSinceEpoch(now + offset, Qt::UTC).toString(Qt::ISODateWithMs); };
        QJsonObject first{{"event", "thinking"}, {"occurredAt", time(-10)}, {"eventId", "one"}};
        QVERIFY(guard.accept("s", &first, now));
        QJsonObject old{{"event", "tool_start"}, {"occurredAt", time(-20)}};
        QVERIFY(!guard.accept("s", &old, now));
        QJsonObject replay{{"event", "thinking"}, {"occurredAt", time(0)}, {"eventId", "one"}};
        QVERIFY(!guard.accept("s", &replay, now));
        QJsonObject missing{{"event", "thinking"}};
        QVERIFY(!guard.accept("s", &missing, now));
        QJsonObject malformed{{"event", "thinking"}, {"sequence", 1}};
        QVERIFY(!guard.accept("other", &malformed, now));
        malformed.insert("runId", "r"); malformed.insert("sequence", 1.5);
        QVERIFY(!guard.accept("other", &malformed, now));
        QJsonObject future{{"event", "thinking"}, {"occurredAt", time(300001)}};
        QVERIFY(!guard.accept("other", &future, now));
        QVERIFY(guard.accept("other-client", &first, now));
    }
    void permissionDecisionsDoNotConsumeSourceSequence() {
        AgentEventGuard guard;
        auto start = event("tool_start", "run", 1);
        QVERIFY(guard.accept("s", &start));
        QJsonObject request{{"event", "permission_requested"}};
        QVERIFY(guard.permissionEvent("s", &request));
        QJsonObject approved{{"event", "permission_approved"}};
        QVERIFY(guard.permissionEvent("s", &approved));
        auto next = event("tool_end", "run", 2);
        QVERIFY(guard.accept("s", &next));
        QJsonObject denied{{"event", "permission_denied"}};
        QVERIFY(guard.permissionEvent("s", &denied));
        auto late = event("tool_start", "run", 3);
        QVERIFY(!guard.accept("s", &late));
    }
};
QTEST_GUILESS_MAIN(EventGuardTest)
#include "tst_eventguard.moc"
