#include "agenteventscheduler.h"
#include "petwindow.h"
#include "testsettings.h"
#include <QTest>
#include <QMouseEvent>

class EventSchedulerTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { isolateTestSettings(); }
    void concurrentToolsStayStableAndRotate() {
        AgentEventScheduler scheduler;
        QVector<AgentEventScheduler::Candidate> items{{"a", "tool", "A"}, {"b", "tool", "B"}};
        QCOMPARE(scheduler.select("main", items, 0), QString("a"));
        for (int now = 10; now < 1800; now += 10) {
            std::reverse(items.begin(), items.end());
            QCOMPARE(scheduler.select("main", items, now), QString("a"));
        }
        QCOMPARE(scheduler.select("main", items, 1800), QString("b"));
        QCOMPARE(scheduler.select("main", items, 3600), QString("a"));
    }
    void urgentPreemptionAndIndependentDestinations() {
        AgentEventScheduler scheduler;
        QVector<AgentEventScheduler::Candidate> items{{"a", "tool", "A"}};
        QCOMPARE(scheduler.select("main", items, 0), QString("a"));
        items.append({"b", "permission", "Approve?"});
        QCOMPARE(scheduler.select("main", items, 1), QString("b"));
        QCOMPARE(scheduler.select("bound", {{"c", "thinking", "C"}}, 1), QString("c"));
        items.removeLast();
        QCOMPARE(scheduler.select("main", items, 2), QString("a"));
        QCOMPARE(scheduler.select("main", {}, 3), QString());
    }
    void coalescesWithoutDelayingUrgentOrFinalState() {
        AgentEventScheduler scheduler;
        QVERIFY(scheduler.present("main", "a", "tool", "first", 0));
        QVERIFY(!scheduler.present("main", "a", "thinking", "second", 1));
        QVERIFY(!scheduler.present("main", "a", "tool", "third", 799));
        QVERIFY(scheduler.present("main", "a", "thinking", "latest", 800));
        QVERIFY(scheduler.present("main", "a", "error", "failed", 801));
        QVERIFY(!scheduler.present("main", "a", "error", "failed", 5000));
    }
    void basketRequiresDownwardCrossingInsideRim() {
        const QRect rim(100, 100, 150, 10);
        QVERIFY(PetWindow::basketballCrossesRim({150, 90}, {160, 120}, rim));
        QVERIFY(!PetWindow::basketballCrossesRim({150, 120}, {160, 90}, rim));
        QVERIFY(!PetWindow::basketballCrossesRim({90, 90}, {95, 120}, rim));
        QVERIFY(!PetWindow::basketballCrossesRim({150, 110}, {160, 120}, rim));
        QVERIFY(!PetWindow::basketballCrossesRim({150, 100}, {160, 100}, rim));
    }
    void gameLifecycle() {
        PetWindow pet;
        pet.show();
        const QPoint original = pet.pos();
        pet.setBasketballEnabled(true);
        QVERIFY(pet.basketballEnabled());
        QCOMPARE(pet.basketballScore(), 0);
        QCOMPARE(pet.basketballShots(), 0);
        const QPointF local(60, 80);
        const QPointF global = pet.mapToGlobal(local.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&pet, &press);
        QMouseEvent drag(QEvent::MouseMove, local + QPointF(40, -40), global + QPointF(40, -40), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&pet, &drag);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global + QPointF(40, -40), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&pet, &release);
        QCOMPARE(pet.basketballShots(), 1);
        QTest::qWait(100);
        pet.setBasketballEnabled(false);
        QVERIFY(!pet.basketballEnabled());
        QCOMPARE(pet.pos(), original);
        pet.setBasketballEnabled(true);
        pet.hide();
        QVERIFY(!pet.basketballEnabled());
    }
};
QTEST_MAIN(EventSchedulerTest)
#include "tst_eventscheduler.moc"
