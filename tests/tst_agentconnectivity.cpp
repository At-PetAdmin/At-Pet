#include "agentconnectivity.h"
#include "agentmonitor.h"
#include "settingsdialog.h"
#include "testsettings.h"

#include <QCheckBox>
#include <QEvent>
#include <QLabel>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTranslator>

namespace {
QJsonObject agentEvent(const QString &name, const QString &session = QStringLiteral("s1"),
                  const QString &app = QStringLiteral("codex")) {
    return {{"sessionId", session}, {"agentApp", app}, {"event", name}};
}

QJsonObject state(const AgentConnectivity &tracker, const QString &key = QStringLiteral("openaicodex")) {
    return tracker.snapshot().value(key).toObject();
}

class PrefixTranslator final : public QTranslator {
    QString translate(const char *, const char *source, const char *, int) const override {
        return QStringLiteral("translated-") + QString::fromUtf8(source);
    }
};
}

class AgentConnectivityTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        isolateTestSettings();
        QCoreApplication::setOrganizationName(QStringLiteral("AtPetTests"));
        QCoreApplication::setApplicationName(QStringLiteral("AgentConnectivity"));
        QStandardPaths::setTestModeEnabled(true);
    }

    void init() { QSettings().clear(); }

    void connectsOnceAcrossAliasesAndTaskCompletion() {
        AgentConnectivity tracker;
        QSignalSpy connected(&tracker, &AgentConnectivity::agentConnected);
        tracker.recordEvent(agentEvent("thinking"));
        tracker.recordEvent(agentEvent("tool_start", "s1", "OpenAI Codex"));
        tracker.recordEvent(agentEvent("done"));
        tracker.recordEvent(agentEvent("idle"));
        QCOMPARE(connected.count(), 1);
        QCOMPARE(connected.first().first().toString(), QStringLiteral("OpenAI Codex"));
        QCOMPARE(state(tracker).value("status").toString(), QStringLiteral("connected"));
        QCOMPARE(state(tracker).value("sessionCount").toInt(), 1);
    }

    void disconnectWaitsForLastSessionAndReconnects() {
        AgentConnectivity tracker;
        QSignalSpy connected(&tracker, &AgentConnectivity::agentConnected);
        tracker.recordEvent(agentEvent("connected"));
        tracker.recordEvent(agentEvent("thinking", "s2"));
        tracker.recordEvent(agentEvent("session_end"));
        QVERIFY(state(tracker).value("connected").toBool());
        QCOMPARE(state(tracker).value("sessionCount").toInt(), 1);
        tracker.recordEvent(agentEvent("session_end", "s2"));
        QCOMPARE(state(tracker).value("status").toString(), QStringLiteral("disconnected"));
        tracker.recordEvent(agentEvent("connected", "s3"));
        QCOMPARE(connected.count(), 2);
    }

    void endingAnUnseenSessionDoesNotAnnounce() {
        AgentConnectivity tracker;
        QSignalSpy connected(&tracker, &AgentConnectivity::agentConnected);
        tracker.recordEvent(agentEvent("session_end"));
        QCOMPARE(connected.count(), 0);
        QCOMPARE(state(tracker).value("status").toString(), QStringLiteral("disconnected"));
    }

    void expiresSessionsIndividuallyAndReconnectsAfterSilence() {
        AgentConnectivity tracker;
        QSignalSpy connected(&tracker, &AgentConnectivity::agentConnected);
        const QDateTime start = QDateTime::fromString(QStringLiteral("2026-11-05T00:00:00Z"), Qt::ISODate);
        tracker.recordEvent(agentEvent("thinking"), start);
        tracker.recordEvent(agentEvent("thinking", "s2"), start.addSecs(600));
        tracker.expire(start.addSecs(1800));
        QCOMPARE(state(tracker).value("sessionCount").toInt(), 1);
        tracker.expire(start.addSecs(2400));
        QCOMPARE(state(tracker).value("status").toString(), QStringLiteral("inactive"));
        QCOMPARE(state(tracker).value("lastSeen").toString(), start.addSecs(600).toString(Qt::ISODateWithMs));
        tracker.recordEvent(agentEvent("thinking"), start.addSecs(2500));
        QCOMPARE(connected.count(), 2);
        // A new event must notice expiry even before the housekeeping timer runs.
        tracker.recordEvent(agentEvent("thinking"), start.addSecs(4400));
        QCOMPARE(connected.count(), 3);
    }

    void subagentsShareTheirParentConnection() {
        AgentConnectivity tracker;
        QSignalSpy connected(&tracker, &AgentConnectivity::agentConnected);
        tracker.recordEvent(agentEvent("connected"));
        QJsonObject child = agentEvent("thinking", "child");
        child.insert("parentSessionId", "s1");
        tracker.recordEvent(child);
        child.insert("event", "session_end");
        tracker.recordEvent(child);
        QCOMPARE(state(tracker).value("sessionCount").toInt(), 1);
        QVERIFY(state(tracker).value("connected").toBool());
        tracker.recordEvent(agentEvent("session_end"));
        QVERIFY(!state(tracker).value("connected").toBool());
        QCOMPARE(connected.count(), 1);
    }

    void ignoresAnonymousOrMalformedEvents() {
        AgentConnectivity tracker;
        tracker.recordEvent(agentEvent("thinking", "s1", ""));
        tracker.recordEvent(agentEvent("thinking", ""));
        tracker.recordEvent(agentEvent("   "));
        QVERIFY(tracker.snapshot().isEmpty());
    }

    void questionContactAndDifferentAgents() {
        AgentConnectivity tracker;
        QSignalSpy connected(&tracker, &AgentConnectivity::agentConnected);
        tracker.recordContact("claude-code", "");
        tracker.recordContact("Claude Code", "");
        tracker.recordEvent(agentEvent("thinking"));
        QCOMPARE(connected.count(), 2);
        QCOMPARE(state(tracker, "claudecode").value("connected").toBool(), true);
        QCOMPARE(state(tracker).value("connected").toBool(), true);
    }

    void nativeAdaptersDistinguishIdleFromDisconnect() {
        AgentConnectivity tracker;
        const auto *claude = agentMonitorStrategy(AgentMonitorType::ClaudeCodeHttpHooks);
        const auto *opencode = agentMonitorStrategy(AgentMonitorType::OpenCodeV2Plugin);
        QJsonObject normalized;
        QString error;
        QVERIFY(claude->normalizeEvent({{"session_id", "c1"}, {"hook_event_name", "SessionStart"}}, &normalized, &error));
        tracker.recordEvent(normalized);
        QVERIFY(state(tracker, "claudecode").value("connected").toBool());
        QVERIFY(claude->normalizeEvent({{"session_id", "c1"}, {"hook_event_name", "SessionEnd"}}, &normalized, &error));
        tracker.recordEvent(normalized);
        QVERIFY(!state(tracker, "claudecode").value("connected").toBool());

        for (const QString &source : {QStringLiteral("session.created"), QStringLiteral("session.idle")}) {
            QVERIFY(opencode->normalizeEvent({{"sessionID", "o1"}, {"sourceEvent", source}}, &normalized, &error));
            tracker.recordEvent(normalized);
            QVERIFY(state(tracker, "opencode").value("connected").toBool());
        }
        QVERIFY(opencode->normalizeEvent({{"sessionID", "o1"}, {"sourceEvent", "session.deleted"}}, &normalized, &error));
        tracker.recordEvent(normalized);
        QVERIFY(!state(tracker, "opencode").value("connected").toBool());
    }

    void settingsFollowsLiveStateReopeningAndLanguageChanges() {
        AgentConnectivity tracker;
        SettingsDialog dialog;
        const auto label = dialog.findChild<QLabel *>(QStringLiteral("agentConnection_openaicodex"));
        QVERIFY(label);
        QVERIFY(label->text().contains("Not connected"));
        connect(&tracker, &AgentConnectivity::changed, &dialog, [&] {
            dialog.setAgentConnections(tracker.snapshot());
        });
        // Event filters control reactions, not whether a sender has contacted us.
        QSettings().setValue("agents/monitor_openaicodex", false);
        tracker.recordEvent(agentEvent("thinking"));
        QVERIFY(label->text().contains("Connected"));
        QVERIFY(label->toolTip().contains("Last event:"));

        SettingsDialog reopened;
        reopened.setAgentConnections(tracker.snapshot());
        QVERIFY(reopened.findChild<QLabel *>(QStringLiteral("agentConnection_openaicodex"))->text().contains("Connected"));
        PrefixTranslator translator;
        QCoreApplication::installTranslator(&translator);
        QEvent languageChange(QEvent::LanguageChange);
        QCoreApplication::sendEvent(&dialog, &languageChange);
        QVERIFY(label->text().contains("translated-Connected"));
        QCoreApplication::removeTranslator(&translator);

        tracker.recordEvent(agentEvent("session_end"));
        QVERIFY(label->text().contains("Disconnected"));
        tracker.recordEvent(agentEvent("thinking"));
        tracker.expire(QDateTime::currentDateTimeUtc().addSecs(1801));
        QVERIFY(label->text().contains("No recent events"));
    }
};

QTEST_MAIN(AgentConnectivityTest)
#include "tst_agentconnectivity.moc"
