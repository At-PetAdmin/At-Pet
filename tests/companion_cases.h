#pragma once
#include <QTest>
#include <QComboBox>
#include <QCheckBox>
#include <QTableWidget>
#include <QListWidget>
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
    void concurrentEventsUseStablePresentation() {
        PetController controller;
        const auto send = [&](const QString &id, const QString &event, const QString &text) {
            controller.onAgentEvent({{"sessionId", id}, {"event", event},
                {"agentApp", "claude-code"}, {"message", text}});
        };
        send("first", "tool_start", "first tool");
        auto pet = controller.visiblePets().first();
        const QString firstSpeech = pet->speech();
        for (int i = 0; i < 100; ++i) {
            send("second", "tool_start", "second tool");
            send("first", "tool_end", "first updated");
        }
        QCOMPARE(pet->speech(), firstSpeech);
        QCOMPARE(controller.sessions_.size(), 2);
        send("second", "permission_requested", "approval");
        QCOMPARE(pet->animation(), QString("permission"));
        controller.clearPets();
    }
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
        controller.sessions_.clear();
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
    void builtinPetSelectionAndBinding() {
        const QString first = petcatalog::bundledIds().first();
        QSettings().setValue("pet/selected", first);
        PetController controller;
        QCOMPARE(controller.visiblePets().first()->petId(), first);
        const QString second = petcatalog::bundledIds().at(1);
        controller.switchPet(second);
        QCOMPARE(QSettings().value("pet/selected").toString(), second);
        QCOMPARE(controller.visiblePets().first()->petId(), second);
        controller.setSubagentPetsEnabled(true);
        controller.onAgentEvent({{"sessionId", "builtin-subagent"}, {"event", "thinking"},
            {"agentApp", "claude-code"}, {"isSubagent", true}});
        QVERIFY(!controller.subagentPetWindows_.isEmpty());
        for (auto pet : controller.subagentPetWindows_) QCOMPARE(pet->petId(), second);
        const auto key = agentAppKey(normalizeAgentApp("claude-code"));
        QSettings().setValue("agents/pet/" + key, first);
        controller.onAgentEvent({{"sessionId", "builtin-bound"}, {"event", "thinking"}, {"agentApp", "claude-code"}});
        QCOMPARE(controller.boundPetWindows_.value(key)->petId(), first);
        controller.setSubagentPetsEnabled(false);
        QSettings().remove("agents/pet/" + key);
        controller.summonAll();
        QSet<QString> summoned;
        for (auto pet : controller.visiblePets()) summoned.insert(pet->petId());
        QVERIFY(summoned.contains("default"));
        for (const QString &id : petcatalog::bundledIds()) QVERIFY(summoned.contains(id));
        for (const QString &id : petcatalog::bundledIds()) controller.deletePet(id);
        QCOMPARE(controller.visiblePets().size(), summoned.size());
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
