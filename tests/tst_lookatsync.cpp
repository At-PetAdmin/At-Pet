// Regression test for the "Look at the mouse cursor" toggle desync between the pet
// context menu and the Settings dialog.
//
// This is the same contract the gravity test asserts (tests/tst_gravitysync.cpp): the
// controller owns the state, persists it, and pushes it into the dialog; the dialog only
// renders it and reports user intent back through its signal.
//
// Contract under test (single source of truth = PetController::lookAtMouseEnabled_ mirrored
// to QSettings pet/lookAtMouse):
//   1. SettingsDialog::setLookAtMouseEnabled() renders pushed state without emitting
//      lookAtMouseToggled() back (QSignalBlocker, no feedback loop).
//   2. A controller-side change made while the dialog is open is visible immediately.
//   3. A controller-side change made while the dialog is hidden is visible on the next show.
//   4. Widget seeding never writes; the legacy pet/lookAtCopilot migration happens once, in
//      the controller, not in the dialog.
//   5. "Apply General Settings" reports intent through signals only; the controller owns
//      persistence, so the dialog keeps no second copy of pet/lookAtMouse.
//
// PetController lives in app/main.cpp and is not linked here, so FakeController mirrors
// PetController::setLookAtMouseEnabled() and the connection wired in showSettingsDialog().

#include "petsettings.h"
#include "petwindow.h"
#include "settingsdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

namespace {

const QString lookAtMouseKey = QStringLiteral("pet/lookAtMouse");
const QString legacyLookAtCopilotKey = QStringLiteral("pet/lookAtCopilot");

// Mirrors PetController: owns the authoritative value, persists it, pushes it to the dialog.
class FakeController final : public QObject {
public:
    explicit FakeController(SettingsDialog *dialog) : dialog_(dialog) {
        lookAtMouseEnabled_ = petsettings::loadLookAtMouse();
        // Same as app/main.cpp: the dialog reports intent, the controller applies and pushes.
        connect(dialog_, &SettingsDialog::lookAtMouseToggled, this, &FakeController::setLookAtMouseEnabled);
        dialog_->setLookAtMouseEnabled(lookAtMouseEnabled_);
    }

    void setLookAtMouseEnabled(bool enabled) {
        if (lookAtMouseEnabled_ == enabled)
            return;
        lookAtMouseEnabled_ = enabled;
        petsettings::storeLookAtMouse(enabled);
        dialog_->setLookAtMouseEnabled(enabled);
    }

    bool lookAtMouseEnabled() const { return lookAtMouseEnabled_; }

private:
    SettingsDialog *dialog_ = nullptr;
    bool lookAtMouseEnabled_ = false;
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

// The checkbox has no object name in settingsdialog.cpp (only gravityCheck_ has one), so the
// dialog exposes it through this property instead.
QCheckBox *lookAtMouseCheck(SettingsDialog *dialog) {
    const auto boxes = dialog->findChildren<QCheckBox *>();
    for (QCheckBox *box : boxes) {
        if (box->text().contains(QStringLiteral("Eyes follow the mouse")))
            return box;
    }
    return nullptr;
}

// Does exactly what the "Apply General Settings" button does.
void applyGeneralSettings(SettingsDialog *dialog) {
    QVERIFY(QMetaObject::invokeMethod(dialog, "onSaveGeneralSettings"));
    QCoreApplication::processEvents();
}

} // namespace

class LookAtMouseSyncTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void dialogSeedsFromPersistedValue();
    void pushUpdatesCheckbox();
    void pushDoesNotEmitLookAtMouseToggled();
    void pushIsIdempotent();
    void pushOverridesStalePersistedValue();
    void openDialogFollowsControllerChange();
    void hiddenDialogFollowsControllerChange();
    void applyReachesControllerExactlyOnce();
    void applyDoesNotReboundOnDialog();
    void dialogDoesNotPersistItsOwnLookAtMouseCopy();
    void constructionDoesNotMigrateOrWrite();
    void petWindowMenuSourceFollowsController();

private:
    ModalCloser *modalCloser_ = nullptr;
};

void LookAtMouseSyncTest::initTestCase() {
    // Keep the developer's real At-Pet settings untouched.
    QCoreApplication::setOrganizationName(QStringLiteral("At-Pet-Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("LookAtMouseSyncTest"));
}

void LookAtMouseSyncTest::init() {
    QSettings().clear();
    QSettings().sync();
    modalCloser_ = new ModalCloser;
}

void LookAtMouseSyncTest::cleanup() {
    delete modalCloser_;
    modalCloser_ = nullptr;
    QSettings().clear();
    QSettings().sync();
}

// Behaviour that already worked before the fix: the constructor seeds from QSettings.
void LookAtMouseSyncTest::dialogSeedsFromPersistedValue() {
    QSettings().setValue(lookAtMouseKey, true);
    SettingsDialog dialog;
    QCheckBox *check = lookAtMouseCheck(&dialog);
    QVERIFY(check);
    QVERIFY(check->isChecked());
}

// The core fix: pushing controller state onto the open dialog updates the widget.
void LookAtMouseSyncTest::pushUpdatesCheckbox() {
    QSettings().setValue(lookAtMouseKey, false);
    SettingsDialog dialog;
    dialog.show();
    QCheckBox *check = lookAtMouseCheck(&dialog);
    QVERIFY(check);
    QVERIFY(!check->isChecked());

    dialog.setLookAtMouseEnabled(true);
    QVERIFY(check->isChecked());

    dialog.setLookAtMouseEnabled(false);
    QVERIFY(!check->isChecked());
}

// Anti-feedback-loop: the push must not look like a user edit.
void LookAtMouseSyncTest::pushDoesNotEmitLookAtMouseToggled() {
    QSettings().setValue(lookAtMouseKey, false);
    SettingsDialog dialog;
    QSignalSpy spy(&dialog, &SettingsDialog::lookAtMouseToggled);

    dialog.setLookAtMouseEnabled(true);
    dialog.setLookAtMouseEnabled(false);
    dialog.setLookAtMouseEnabled(true);

    QCOMPARE(spy.count(), 0);
}

void LookAtMouseSyncTest::pushIsIdempotent() {
    SettingsDialog dialog;
    QSignalSpy spy(&dialog, &SettingsDialog::lookAtMouseToggled);
    for (int i = 0; i < 5; ++i)
        dialog.setLookAtMouseEnabled(true);
    QVERIFY(lookAtMouseCheck(&dialog)->isChecked());
    QCOMPARE(spy.count(), 0);
}

// Constructor race: the dialog seeds itself from QSettings during construction, which can be
// stale compared with the controller's live value. The push that follows must win.
void LookAtMouseSyncTest::pushOverridesStalePersistedValue() {
    QSettings().setValue(lookAtMouseKey, false);
    SettingsDialog dialog;
    QVERIFY(!lookAtMouseCheck(&dialog)->isChecked()); // stale copy taken from QSettings

    dialog.setLookAtMouseEnabled(true);               // what the controller actually believes
    QVERIFY(lookAtMouseCheck(&dialog)->isChecked());
    // Pushing state must not rewrite the persisted value behind the controller's back.
    QCOMPARE(QSettings().value(lookAtMouseKey).toBool(), false);
}

// Dialog already open: a context-menu change must show up immediately.
void LookAtMouseSyncTest::openDialogFollowsControllerChange() {
    QSettings().setValue(lookAtMouseKey, false);
    SettingsDialog dialog;
    FakeController controller(&dialog);
    dialog.show();
    QVERIFY(!lookAtMouseCheck(&dialog)->isChecked());

    controller.setLookAtMouseEnabled(true); // user ticked the entry in the pet context menu
    QVERIFY(lookAtMouseCheck(&dialog)->isChecked());
    QVERIFY(QSettings().value(lookAtMouseKey).toBool());

    controller.setLookAtMouseEnabled(false);
    QVERIFY(!lookAtMouseCheck(&dialog)->isChecked());
    QVERIFY(!QSettings().value(lookAtMouseKey).toBool());
}

// Dialog hidden: the change must be visible the next time it is shown.
void LookAtMouseSyncTest::hiddenDialogFollowsControllerChange() {
    QSettings().setValue(lookAtMouseKey, false);
    SettingsDialog dialog;
    FakeController controller(&dialog);
    dialog.show();
    dialog.hide();

    controller.setLookAtMouseEnabled(true);
    dialog.show();
    QVERIFY(lookAtMouseCheck(&dialog)->isChecked());

    dialog.hide();
    controller.setLookAtMouseEnabled(false);
    dialog.show();
    QVERIFY(!lookAtMouseCheck(&dialog)->isChecked());
}

// Dialog -> controller direction: one emission per Apply, controller and storage updated.
void LookAtMouseSyncTest::applyReachesControllerExactlyOnce() {
    QSettings().setValue(lookAtMouseKey, false);
    SettingsDialog dialog;
    FakeController controller(&dialog);
    dialog.show();
    QSignalSpy spy(&dialog, &SettingsDialog::lookAtMouseToggled);

    lookAtMouseCheck(&dialog)->click(); // user ticks the box in Preferences
    QCOMPARE(spy.count(), 0);           // nothing is applied until the button is pressed
    applyGeneralSettings(&dialog);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);
    QVERIFY(controller.lookAtMouseEnabled());
    QVERIFY(QSettings().value(lookAtMouseKey).toBool());
    QVERIFY(lookAtMouseCheck(&dialog)->isChecked());

    lookAtMouseCheck(&dialog)->click();
    applyGeneralSettings(&dialog);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
    QVERIFY(!controller.lookAtMouseEnabled());
    QVERIFY(!QSettings().value(lookAtMouseKey).toBool());
}

// Round trip: after the controller echoes back, the widget stays where it was (no bounce).
void LookAtMouseSyncTest::applyDoesNotReboundOnDialog() {
    QSettings().setValue(lookAtMouseKey, false);
    SettingsDialog dialog;
    FakeController controller(&dialog);
    dialog.show();

    lookAtMouseCheck(&dialog)->click();
    applyGeneralSettings(&dialog);
    QVERIFY(lookAtMouseCheck(&dialog)->isChecked());
    QVERIFY(controller.lookAtMouseEnabled());

    controller.setLookAtMouseEnabled(true); // redundant echo
    QCoreApplication::processEvents();
    QVERIFY(lookAtMouseCheck(&dialog)->isChecked());
}

// Requirement "the dialog must not keep its own copy": with no controller attached, Apply only
// reports intent through the signal and must not write pet/lookAtMouse itself. On the pre-fix
// code this fails, because onSaveGeneralSettings() wrote pet/lookAtMouse straight from the
// widget state - the exact step that used to revert the pet to the old value.
void LookAtMouseSyncTest::dialogDoesNotPersistItsOwnLookAtMouseCopy() {
    QSettings().setValue(lookAtMouseKey, false);
    SettingsDialog dialog;
    dialog.show();

    lookAtMouseCheck(&dialog)->click();
    QVERIFY(lookAtMouseCheck(&dialog)->isChecked());
    applyGeneralSettings(&dialog);

    QCOMPARE(QSettings().value(lookAtMouseKey).toBool(), false);
}

// The legacy pet/lookAtCopilot migration belongs to the controller. The dialog must be able to
// render a stored value without performing the migration as a side effect of construction.
void LookAtMouseSyncTest::constructionDoesNotMigrateOrWrite() {
    QSettings settings;
    settings.remove(lookAtMouseKey);                  // fresh install: only the legacy key exists
    settings.setValue(legacyLookAtCopilotKey, true);
    settings.sync();
    {
        SettingsDialog dialog;
        QVERIFY(!lookAtMouseCheck(&dialog)->isChecked()); // renders the default, no migration
    }
    QSettings reread;
    QVERIFY(!reread.contains(lookAtMouseKey));         // constructing the dialog did not migrate
    QVERIFY(reread.contains(legacyLookAtCopilotKey));  // legacy key untouched: controller owns it

    // The controller-side loader performs the migration exactly once.
    QCOMPARE(petsettings::loadLookAtMouse(), true);
    QSettings migrated;
    QCOMPARE(migrated.value(lookAtMouseKey).toBool(), true);
    QVERIFY(!migrated.contains(legacyLookAtCopilotKey));
    // Second call must read the migrated key instead of redoing the migration.
    QCOMPARE(petsettings::loadLookAtMouse(), true);
}

// The pet context menu builds its checkable action from PetWindow::lookAtMouse() every time it
// pops up, and the controller pushes into every visible pet, so that member must follow.
void LookAtMouseSyncTest::petWindowMenuSourceFollowsController() {
    // Braces, not parentheses: "PetWindow pet(QString())" would be a most-vexing-parse
    // function declaration.
    PetWindow pet{QString()};
    QCOMPARE(pet.lookAtMouse(), false);

    pet.setLookAtMouse(true);
    QCOMPARE(pet.lookAtMouse(), true);

    pet.setLookAtMouse(false);
    QCOMPARE(pet.lookAtMouse(), false);
}

QTEST_MAIN(LookAtMouseSyncTest)

#include "tst_lookatsync.moc"
