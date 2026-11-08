#pragma once
#include <QTest>
#include <QComboBox>
#include <QCheckBox>
#include <QTableWidget>
#include "testsettings.h"

class CompanionTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        isolateTestSettings();
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("AtPetTests");
        QCoreApplication::setApplicationName("CompanionTests");
    }
    void init() { QSettings().clear(); }
    void controllerFlow() {
        PetController controller;
        const auto send = [&](const QString &id, const QString &event) {
            controller.onAgentEvent({{"sessionId", id}, {"event", event}, {"agentApp", "claude-code"}, {"message", "testing"}});
        };
        send("one", "thinking");
        QCOMPARE(controller.sessions_.size(), 1);
        send("one", "done");
        QCOMPARE(controller.recentResults_.size(), 1);
        send("one", "done");
        QCOMPARE(controller.recentResults_.size(), 1);
        for (int i = 0; i < 55; ++i) send(QString::number(i), "done");
        QCOMPARE(controller.recentResults_.size(), 50);
        QCOMPARE(controller.pendingCompletions_, 56);
        QTest::qWait(1400);
        QCOMPARE(controller.pendingCompletions_, 0);
        QVERIFY(controller.visiblePets().first()->speech().contains("56"));
        const auto key = agentAppKey(normalizeAgentApp("claude-code"));
        QSettings().setValue("agents/pet/" + key, "default");
        send("bound", "thinking");
        QVERIFY(controller.boundPetWindows_.contains(key));
        QCOMPARE(controller.boundPetWindows_.value(key)->animation(), QString("thinking"));
        QSettings().setValue("agents/pet/" + key, "../invalid");
        controller.applySessionAnimation();
        QVERIFY(controller.boundPetWindows_.isEmpty());
        controller.trackRequest("request", {{"agentApp", "Claude Code"}, {"question", "Choose?"}}, "question");
        QVERIFY(controller.urgentNotification());
        QCOMPARE(controller.stateJson().value("pendingRequests").toArray().size(), 1);
        controller.onQuestionRequestCancelled("request");
        QVERIFY(!controller.urgentNotification());
        send("failure", "error");
        QVERIFY(controller.urgentNotification());
        const QString errorSpeech = controller.visiblePets().first()->speech();
        send("another", "done");
        QTest::qWait(1400);
        QCOMPARE(controller.visiblePets().first()->speech(), errorSpeech);
        controller.clearPets();
    }
    void settingsAndPersonality() {
        QSettings().setValue("pets/personality/default", "quiet");
        QSettings().setValue("agents/allowPermissionForwarding", false);
        SettingsDialog dialog;
        auto *personality = dialog.findChild<QComboBox *>("personalityCombo");
        QVERIFY(personality);
        QCOMPARE(personality->currentData().toString(), QString("quiet"));
        QVERIFY(!dialog.findChild<QCheckBox *>("permissionForwardingCheck")->isChecked());
        dialog.setTasks({{"recentResults", QJsonArray{QJsonObject{{"animation", "error"}, {"message", "<b>literal</b>"}}}}});
        auto *table = dialog.findChild<QTableWidget *>("tasksTable");
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 3)->text(), QString("<b>literal</b>"));
        dialog.show();
        dialog.showTasks();
        QTest::qWait(50);
        QVERIFY(table->isVisible());
    }
};
QTEST_MAIN(CompanionTest)
