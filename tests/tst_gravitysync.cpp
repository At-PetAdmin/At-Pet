// Regression test for the Gravity toggle desync between the pet context menu and
// the Settings dialog.
//
// Contract under test (single source of truth = the controller / QSettings pet/gravity):
//   1. SettingsDialog::setGravityEnabled() renders pushed state without emitting
//      gravityToggled() back (QSignalBlocker, no feedback loop).
//   2. A controller-side change made while the dialog is open is visible immediately.
//   3. A controller-side change made while the dialog is hidden is visible on the next show.
//   4. Widget seeding (constructor / loadSettings) never reports user intent.
//   5. "Apply General Settings" reports intent through signals only; the controller owns
//      persistence, the dialog keeps no second copy of pet/gravity.
//
// The controller itself lives in app/main.cpp and is not linked here, so the test drives a
// FakeController that mirrors PetController::setGravityEnabled() plus the connection made at
// app/main.cpp (SettingsDialog::gravityToggled -> PetController::setGravityEnabled).

#include "petwindow.h"
#include "settingsdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QMessageBox>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

namespace {

const QString petGravityKey = QStringLiteral("pet/gravity");

// Mirrors PetController: owns the authoritative value, persists it, pushes it to the dialog.
class FakeController final : public QObject {
public:
    explicit FakeController(SettingsDialog *dialog) : dialog_(dialog) {
        gravityEnabled_ = QSettings().value(petGravityKey, false).toBool();
        // Same as app/main.cpp: the dialog reports intent, the controller applies and pushes.
        connect(dialog_, &SettingsDialog::gravityToggled, this, &FakeController::setGravityEnabled);
        dialog_->setGravityEnabled(gravityEnabled_);
    }

    void setGravityEnabled(bool enabled) {
        if (gravityEnabled_ == enabled)
            return;
        gravityEnabled_ = enabled;
        QSettings().setValue(petGravityKey, enabled);
        dialog_->setGravityEnabled(enabled);
    }

    bool gravityEnabled() const { return gravityEnabled_; }

private:
    SettingsDialog *dialog_ = nullptr;
    bool gravityEnabled_ = false;
};

// QMessageBox::information() runs a nested event loop; auto-dismiss any modal box so the
// Apply path cannot deadlock an offscreen test run.
class ModalCloser final : public QObject {
public:
    ModalCloser() {
        timer_.setInterval(10);
        connect(&timer_, &QTimer::timeout, this, [] {
            if (QWidget *modal = QApplication::activeModalWidget())
                modal->close();
        });
        timer_.start();
    }

private:
    QTimer timer_;
};

QCheckBox *gravityCheck(SettingsDialog *dialog) {
    return dialog->findChild<QCheckBox *>(QStringLiteral("gravityCheck"));
}

// Does exactly what the "Apply General Settings" button does.
void applyGeneralSettings(SettingsDialog *dialog) {
    QVERIFY(QMetaObject::invokeMethod(dialog, "onSaveGeneralSettings"));
    QCoreApplication::processEvents();
}

} // namespace

class GravitySyncTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void dialogSeedsFromPersistedValue();
    void constructionDoesNotWriteBack();
    void pushUpdatesCheckbox();
    void pushDoesNotEmitGravityToggled();
    void pushIsIdempotent();
    void pushOverridesStalePersistedValue();
    void openDialogFollowsControllerChange();
    void hiddenDialogFollowsControllerChange();
    void applyReachesControllerExactlyOnce();
    void applyDoesNotReboundOnDialog();
    void dialogDoesNotPersistItsOwnGravityCopy();
    void petWindowMenuSourceFollowsController();

private:
    ModalCloser *modalCloser_ = nullptr;
};

void GravitySyncTest::initTestCase() {
    // Keep the developer's real At-Pet settings untouched.
    QCoreApplication::setOrganizationName(QStringLiteral("At-Pet-Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("GravitySyncTest"));
}

void GravitySyncTest::init() {
    QSettings().clear();
    QSettings().sync();
    modalCloser_ = new ModalCloser;
}

void GravitySyncTest::cleanup() {
    delete modalCloser_;
    modalCloser_ = nullptr;
    QSettings().clear();
    QSettings().sync();
}

// Behaviour that already worked before the fix: the constructor seeds from QSettings.
void GravitySyncTest::dialogSeedsFromPersistedValue() {
    QSettings().setValue(petGravityKey, true);
    SettingsDialog dialog;
    QVERIFY(gravityCheck(&dialog));
    QVERIFY(gravityCheck(&dialog)->isChecked());
}

// Seeding widgets must not leak back into storage. Signals emitted during construction cannot
// be observed with QSignalSpy (spies attach to an instance that does not exist yet), so assert
// the observable half: constructing the dialog never rewrites the keys it seeds from.
void GravitySyncTest::constructionDoesNotWriteBack() {
    QSettings settings;
    settings.setValue(petGravityKey, false);
    settings.setValue(QStringLiteral("pet/lookAtMouse"), false);
    settings.setValue(QStringLiteral("pet/autoDoNotDisturb"), true);
    settings.setValue(QStringLiteral("feedback/completionSound"), true);
    settings.sync();
    {
        SettingsDialog dialog;
        QVERIFY(!gravityCheck(&dialog)->isChecked());
    }
    QSettings reread;
    QCOMPARE(reread.value(petGravityKey).toBool(), false);
    QCOMPARE(reread.value(QStringLiteral("pet/lookAtMouse")).toBool(), false);
    QCOMPARE(reread.value(QStringLiteral("pet/autoDoNotDisturb")).toBool(), true);
    QCOMPARE(reread.value(QStringLiteral("feedback/completionSound")).toBool(), true);
}

// The core fix: pushing controller state onto the open dialog updates the widget.
void GravitySyncTest::pushUpdatesCheckbox() {
    QSettings().setValue(petGravityKey, false);
    SettingsDialog dialog;
    dialog.show();
    QVERIFY(!gravityCheck(&dialog)->isChecked());

    dialog.setGravityEnabled(true);
    QVERIFY(gravityCheck(&dialog)->isChecked());

    dialog.setGravityEnabled(false);
    QVERIFY(!gravityCheck(&dialog)->isChecked());
}

// Anti-feedback-loop: the push must not look like a user edit.
void GravitySyncTest::pushDoesNotEmitGravityToggled() {
    QSettings().setValue(petGravityKey, false);
    SettingsDialog dialog;
    QSignalSpy spy(&dialog, &SettingsDialog::gravityToggled);

    dialog.setGravityEnabled(true);
    dialog.setGravityEnabled(false);
    dialog.setGravityEnabled(true);

    QCOMPARE(spy.count(), 0);
}

// Repeated pushes with an unchanged value must be harmless (no oscillation, no extra signals).
void GravitySyncTest::pushIsIdempotent() {
    SettingsDialog dialog;
    QSignalSpy spy(&dialog, &SettingsDialog::gravityToggled);
    for (int i = 0; i < 5; ++i)
        dialog.setGravityEnabled(true);
    QVERIFY(gravityCheck(&dialog)->isChecked());
    QCOMPARE(spy.count(), 0);
}

// Constructor race: the dialog seeds itself from QSettings during construction, which can be
// stale compared with the controller's live value. The push that follows must win.
void GravitySyncTest::pushOverridesStalePersistedValue() {
    QSettings().setValue(petGravityKey, false);
    SettingsDialog dialog;
    QVERIFY(!gravityCheck(&dialog)->isChecked()); // stale copy taken from QSettings

    dialog.setGravityEnabled(true);               // what the controller actually believes
    QVERIFY(gravityCheck(&dialog)->isChecked());
    // Pushing state must not rewrite the persisted value behind the controller's back.
    QCOMPARE(QSettings().value(petGravityKey).toBool(), false);
}

// Dialog already open: a context-menu change must show up immediately.
void GravitySyncTest::openDialogFollowsControllerChange() {
    QSettings().setValue(petGravityKey, false);
    SettingsDialog dialog;
    FakeController controller(&dialog);
    dialog.show();
    QVERIFY(!gravityCheck(&dialog)->isChecked());

    controller.setGravityEnabled(true); // user ticked Gravity in the pet context menu
    QVERIFY(gravityCheck(&dialog)->isChecked());
    QVERIFY(QSettings().value(petGravityKey).toBool());

    controller.setGravityEnabled(false);
    QVERIFY(!gravityCheck(&dialog)->isChecked());
    QVERIFY(!QSettings().value(petGravityKey).toBool());
}

// Dialog hidden: the change must be visible the next time it is shown.
void GravitySyncTest::hiddenDialogFollowsControllerChange() {
    QSettings().setValue(petGravityKey, false);
    SettingsDialog dialog;
    FakeController controller(&dialog);
    dialog.show();
    dialog.hide();

    controller.setGravityEnabled(true);
    dialog.show();
    QVERIFY(gravityCheck(&dialog)->isChecked());

    dialog.hide();
    controller.setGravityEnabled(false);
    dialog.show();
    QVERIFY(!gravityCheck(&dialog)->isChecked());
}

// Dialog -> controller direction: one emission per Apply, controller and storage updated.
void GravitySyncTest::applyReachesControllerExactlyOnce() {
    QSettings().setValue(petGravityKey, false);
    SettingsDialog dialog;
    FakeController controller(&dialog);
    dialog.show();
    QSignalSpy spy(&dialog, &SettingsDialog::gravityToggled);

    gravityCheck(&dialog)->click(); // user ticks the box in Preferences
    QCOMPARE(spy.count(), 0);       // nothing is applied until the button is pressed
    applyGeneralSettings(&dialog);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);
    QVERIFY(controller.gravityEnabled());
    QVERIFY(QSettings().value(petGravityKey).toBool());
    QVERIFY(gravityCheck(&dialog)->isChecked());

    gravityCheck(&dialog)->click();
    applyGeneralSettings(&dialog);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
    QVERIFY(!controller.gravityEnabled());
    QVERIFY(!QSettings().value(petGravityKey).toBool());
}

// Round trip: after the controller echoes back, the widget stays where it was (no bounce).
void GravitySyncTest::applyDoesNotReboundOnDialog() {
    QSettings().setValue(petGravityKey, false);
    SettingsDialog dialog;
    FakeController controller(&dialog);
    dialog.show();

    gravityCheck(&dialog)->click();
    applyGeneralSettings(&dialog);
    QVERIFY(gravityCheck(&dialog)->isChecked());
    QVERIFY(controller.gravityEnabled());

    controller.setGravityEnabled(true); // redundant echo
    QCoreApplication::processEvents();
    QVERIFY(gravityCheck(&dialog)->isChecked());
}

// Requirement "the dialog must not keep its own copy": with no controller attached, Apply only
// reports intent through the signal and must not write pet/gravity itself. On the pre-fix code
// this fails, because onSaveGeneralSettings() wrote pet/gravity straight from the widget state.
void GravitySyncTest::dialogDoesNotPersistItsOwnGravityCopy() {
    QSettings().setValue(petGravityKey, false);
    SettingsDialog dialog;
    dialog.show();

    gravityCheck(&dialog)->click();
    QVERIFY(gravityCheck(&dialog)->isChecked());
    applyGeneralSettings(&dialog);

    QCOMPARE(QSettings().value(petGravityKey).toBool(), false);
}

// The pet context menu builds its checkable actions from PetWindow::gravityEnabled() every
// time it pops up (app/petwindow.cpp: showContextMenu). The controller pushes state into every
// visible pet (main.cpp: setGravityEnabled -> pet->setGravityEnabled), so the member the menu
// reads must follow. That is what makes the menu show the right tick on the next popup.
void GravitySyncTest::petWindowMenuSourceFollowsController() {
    // Braces, not parentheses: "PetWindow pet(QString())" would be a most-vexing-parse
    // function declaration.
    PetWindow pet{QString()};
    QCOMPARE(pet.gravityEnabled(), false);

    pet.setGravityEnabled(true);
    QCOMPARE(pet.gravityEnabled(), true);

    pet.setGravityEnabled(false);
    QCOMPARE(pet.gravityEnabled(), false);
}

QTEST_MAIN(GravitySyncTest)

#include "tst_gravitysync.moc"
