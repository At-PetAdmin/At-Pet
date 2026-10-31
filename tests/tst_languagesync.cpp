// Regression test for the interface-language desync between the tray menu and the Settings
// dialog.
//
// This is the same contract the gravity and look-at-mouse tests assert
// (tests/tst_gravitysync.cpp, tests/tst_lookatsync.cpp): the controller owns the state, persists
// it, and pushes it into the dialog; the dialog only renders it and reports user intent back
// through its signal.
//
// Contract under test (single source of truth = PetController::language_ mirrored to
// QSettings ui/language):
//   1. SettingsDialog::setLanguage() renders pushed state without emitting languageChanged()
//      back (QSignalBlocker, no feedback loop).
//   2. A controller-side change made while the dialog is open is visible immediately.
//   3. A controller-side change made while the dialog is hidden is visible on the next show.
//   4. retranslateUi() only rewrites item text; it neither reads QSettings nor moves the
//      selection, so the controller re-pushes the authoritative language after
//      QEvent::LanguageChange.
//   5. "Apply General Settings" reports intent through its signal only; the controller owns
//      persistence, so the dialog keeps no second copy of ui/language.
//
// PetController lives in app/main.cpp and is not linked here, so FakeController mirrors
// PetController::setLanguage() and updateSettingsCheckboxes().

#include "settingsdialog.h"

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QTranslator>

namespace {

const QString languageKey = QStringLiteral("ui/language");

// Mirrors the fallback in PetController's constructor: anything that is not one of the two
// supported languages is normalised to English.
QString normalizeLanguage(const QString &language) {
    return language == QStringLiteral("zh_CN") ? language : QStringLiteral("en");
}

QString storedLanguage() {
    const QString saved = QSettings().value(languageKey).toString();
    return (saved == QStringLiteral("zh_CN") || saved == QStringLiteral("en"))
        ? saved : QStringLiteral("en");
}

// Mirrors PetController: owns the authoritative language, persists it, pushes it to the dialog.
class FakeController final : public QObject {
public:
    explicit FakeController(SettingsDialog *dialog) : dialog_(dialog) {
        language_ = storedLanguage();
        // Same as app/main.cpp: the dialog reports intent, the controller applies and pushes.
        connect(dialog_, &SettingsDialog::languageChanged, this, &FakeController::setLanguage);
        pushState();
    }

    void setLanguage(const QString &language) {
        if (language_ == language && (!language_.isEmpty()))
            return;
        language_ = normalizeLanguage(language);
        QSettings().setValue(languageKey, language_);
        pushState();
    }

    // Mirrors PetController::updateSettingsCheckboxes(), which showSettingsDialog() calls on
    // every show.
    void pushState() { dialog_->setLanguage(language_); }

    QString language() const { return language_; }

private:
    SettingsDialog *dialog_ = nullptr;
    QString language_;
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

// The language combo has no object name in settingsdialog.cpp, so it is located by the data
// payload of its two fixed entries.
QComboBox *languageCombo(SettingsDialog *dialog) {
    const auto combos = dialog->findChildren<QComboBox *>();
    for (QComboBox *combo : combos) {
        if (combo->count() == 2 && combo->itemData(0).toString() == QStringLiteral("zh_CN") &&
            combo->itemData(1).toString() == QStringLiteral("en"))
            return combo;
    }
    return nullptr;
}

// Reads the selection by data, never by the translated display text.
QString selectedLanguage(SettingsDialog *dialog) {
    QComboBox *combo = languageCombo(dialog);
    return combo ? combo->currentData().toString() : QString();
}

// Simulates the user picking an entry in the combo.
void selectLanguage(SettingsDialog *dialog, const QString &language) {
    QComboBox *combo = languageCombo(dialog);
    QVERIFY(combo);
    const int index = combo->findData(language);
    QVERIFY(index >= 0);
    combo->setCurrentIndex(index);
}

// Does exactly what the "Apply General Settings" button does.
void applyGeneralSettings(SettingsDialog *dialog) {
    QVERIFY(QMetaObject::invokeMethod(dialog, "onSaveGeneralSettings"));
    QCoreApplication::processEvents();
}

// Installing a new translator makes Qt send QEvent::LanguageChange synchronously.
void sendLanguageChange(SettingsDialog *dialog) {
    QEvent event(QEvent::LanguageChange);
    QCoreApplication::sendEvent(dialog, &event);
}

// Rewrites every string, so retranslateUi() performs genuine text updates instead of no-op
// assignments. This is what a real translator swap looks like, without shipping compiled .qm
// resources into the test binary.
class PrefixTranslator final : public QTranslator {
public:
    QString translate(const char *context, const char *sourceText,
                      const char *disambiguation = nullptr, int n = -1) const override {
        Q_UNUSED(context);
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        return QStringLiteral("x-") + QString::fromUtf8(sourceText);
    }
};

} // namespace

class LanguageSyncTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void dialogSeedsFromPersistedValue();
    void pushUpdatesComboSelection();
    void pushDoesNotEmitLanguageChanged();
    void pushIsIdempotent();
    void pushWithUnknownLanguageKeepsSelection();
    void pushOverridesStalePersistedValue();
    void openDialogFollowsControllerChange();
    void hiddenDialogFollowsControllerChange();
    void retranslateKeepsSelection();
    void retranslateWithTranslatorKeepsSelection();
    void traySwitchRetranslateThenPushAligns();
    void applyReachesControllerExactlyOnce();
    void applyDoesNotReboundOnDialog();
    void dialogDoesNotPersistItsOwnLanguageCopy();

private:
    ModalCloser *modalCloser_ = nullptr;
};

void LanguageSyncTest::initTestCase() {
    // Keep the developer's real At-Pet settings untouched.
    QCoreApplication::setOrganizationName(QStringLiteral("At-Pet-Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("LanguageSyncTest"));
}

void LanguageSyncTest::init() {
    QSettings().clear();
    QSettings().sync();
    modalCloser_ = new ModalCloser;
}

void LanguageSyncTest::cleanup() {
    delete modalCloser_;
    modalCloser_ = nullptr;
    QSettings().clear();
    QSettings().sync();
}

// Behaviour that already worked before the fix: the constructor seeds from QSettings.
void LanguageSyncTest::dialogSeedsFromPersistedValue() {
    QSettings().setValue(languageKey, QStringLiteral("en"));
    SettingsDialog dialog;
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("en"));

    QSettings().setValue(languageKey, QStringLiteral("zh_CN"));
    SettingsDialog chineseDialog;
    QCOMPARE(selectedLanguage(&chineseDialog), QStringLiteral("zh_CN"));
}

// The core fix: pushing controller state onto the open dialog moves the selection.
void LanguageSyncTest::pushUpdatesComboSelection() {
    QSettings().setValue(languageKey, QStringLiteral("zh_CN"));
    SettingsDialog dialog;
    dialog.show();
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("zh_CN"));

    dialog.setLanguage(QStringLiteral("en"));
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("en"));

    dialog.setLanguage(QStringLiteral("zh_CN"));
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("zh_CN"));
}

// Anti-feedback-loop: the push must not look like a user edit.
void LanguageSyncTest::pushDoesNotEmitLanguageChanged() {
    QSettings().setValue(languageKey, QStringLiteral("zh_CN"));
    SettingsDialog dialog;
    QSignalSpy spy(&dialog, &SettingsDialog::languageChanged);

    dialog.setLanguage(QStringLiteral("en"));
    dialog.setLanguage(QStringLiteral("zh_CN"));
    dialog.setLanguage(QStringLiteral("en"));

    QCOMPARE(spy.count(), 0);
}

void LanguageSyncTest::pushIsIdempotent() {
    SettingsDialog dialog;
    QSignalSpy spy(&dialog, &SettingsDialog::languageChanged);
    for (int i = 0; i < 5; ++i)
        dialog.setLanguage(QStringLiteral("en"));
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("en"));
    QCOMPARE(spy.count(), 0);
}

// Unknown values are ignored: the slot must never guess or emit.
void LanguageSyncTest::pushWithUnknownLanguageKeepsSelection() {
    QSettings().setValue(languageKey, QStringLiteral("zh_CN"));
    SettingsDialog dialog;
    QSignalSpy spy(&dialog, &SettingsDialog::languageChanged);

    dialog.setLanguage(QStringLiteral("fr"));

    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("zh_CN"));
    QCOMPARE(spy.count(), 0);
}

// Constructor race: the dialog seeds itself from QSettings during construction, which can be
// stale compared with the controller's live value. The push that follows must win.
void LanguageSyncTest::pushOverridesStalePersistedValue() {
    QSettings().setValue(languageKey, QStringLiteral("zh_CN"));
    SettingsDialog dialog;
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("zh_CN")); // stale copy from QSettings

    dialog.setLanguage(QStringLiteral("en"));                     // what the controller believes
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("en"));
    // Pushing state must not rewrite the persisted value behind the controller's back.
    QCOMPARE(QSettings().value(languageKey).toString(), QStringLiteral("zh_CN"));
}

// Dialog already open: a tray-menu change must show up immediately.
void LanguageSyncTest::openDialogFollowsControllerChange() {
    QSettings().setValue(languageKey, QStringLiteral("zh_CN"));
    SettingsDialog dialog;
    FakeController controller(&dialog);
    dialog.show();
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("zh_CN"));

    controller.setLanguage(QStringLiteral("en")); // user picked English in the tray menu
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("en"));
    QCOMPARE(QSettings().value(languageKey).toString(), QStringLiteral("en"));

    controller.setLanguage(QStringLiteral("zh_CN"));
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("zh_CN"));
    QCOMPARE(QSettings().value(languageKey).toString(), QStringLiteral("zh_CN"));
}

// Dialog hidden: the change must be visible the next time it is shown. showSettingsDialog()
// re-pushes on every show, which the FakeController mirrors with pushState().
void LanguageSyncTest::hiddenDialogFollowsControllerChange() {
    QSettings().setValue(languageKey, QStringLiteral("zh_CN"));
    SettingsDialog dialog;
    FakeController controller(&dialog);
    dialog.show();
    dialog.hide();

    controller.setLanguage(QStringLiteral("en"));
    controller.pushState(); // updateSettingsCheckboxes() on the next show
    dialog.show();
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("en"));

    dialog.hide();
    controller.setLanguage(QStringLiteral("zh_CN"));
    controller.pushState();
    dialog.show();
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("zh_CN"));
}

// Root cause 3: retranslateUi() rewrites the combo item text after QEvent::LanguageChange. It
// must not reseed the selection from QSettings (which is the stale copy) or clear it.
void LanguageSyncTest::retranslateKeepsSelection() {
    QSettings().setValue(languageKey, QStringLiteral("en"));
    SettingsDialog dialog;
    dialog.show();
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("en"));

    // Simulate another writer leaving a stale value behind, then a translator swap.
    QSettings().setValue(languageKey, QStringLiteral("zh_CN"));
    sendLanguageChange(&dialog);

    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("en"));
}

// Same contract with a translator actually installed: installTranslator()/removeTranslator()
// deliver QEvent::LanguageChange themselves, exactly like PetController does on a tray switch.
void LanguageSyncTest::retranslateWithTranslatorKeepsSelection() {
    QSettings().setValue(languageKey, QStringLiteral("en"));
    SettingsDialog dialog;
    dialog.show();
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("en"));

    QSettings().setValue(languageKey, QStringLiteral("zh_CN")); // stale value from another writer
    PrefixTranslator translator;
    QCoreApplication::installTranslator(&translator);
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("en"));
    dialog.setLanguage(QStringLiteral("zh_CN")); // controller push after the swap
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("zh_CN"));
    QCoreApplication::removeTranslator(&translator);
}

// The production order in PetController::setLanguage(): installing/removing the translator emits
// LanguageChange (text only), then the controller pushes the authoritative language.
void LanguageSyncTest::traySwitchRetranslateThenPushAligns() {
    QSettings().setValue(languageKey, QStringLiteral("zh_CN"));
    SettingsDialog dialog;
    FakeController controller(&dialog);
    dialog.show();
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("zh_CN"));

    sendLanguageChange(&dialog); // installTranslator(:/i18n/at-pet_en.qm) equivalent
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("zh_CN"));

    controller.setLanguage(QStringLiteral("en"));
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("en"));

    sendLanguageChange(&dialog); // removeTranslator() on the way back
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("en"));
}

// Dialog -> controller direction: one emission per Apply, controller and storage updated.
void LanguageSyncTest::applyReachesControllerExactlyOnce() {
    QSettings().setValue(languageKey, QStringLiteral("zh_CN"));
    SettingsDialog dialog;
    FakeController controller(&dialog);
    dialog.show();
    QSignalSpy spy(&dialog, &SettingsDialog::languageChanged);

    selectLanguage(&dialog, QStringLiteral("en")); // user picks English in Preferences
    QCOMPARE(spy.count(), 0);                      // nothing is applied until the button is pressed
    QCOMPARE(controller.language(), QStringLiteral("zh_CN"));
    applyGeneralSettings(&dialog);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("en"));
    QCOMPARE(controller.language(), QStringLiteral("en"));
    QCOMPARE(QSettings().value(languageKey).toString(), QStringLiteral("en"));
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("en"));

    selectLanguage(&dialog, QStringLiteral("zh_CN"));
    applyGeneralSettings(&dialog);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("zh_CN"));
    QCOMPARE(controller.language(), QStringLiteral("zh_CN"));
    QCOMPARE(QSettings().value(languageKey).toString(), QStringLiteral("zh_CN"));
}

// Round trip: after the controller echoes back, the selection stays where the user put it.
void LanguageSyncTest::applyDoesNotReboundOnDialog() {
    QSettings().setValue(languageKey, QStringLiteral("zh_CN"));
    SettingsDialog dialog;
    FakeController controller(&dialog);
    dialog.show();

    selectLanguage(&dialog, QStringLiteral("en"));
    applyGeneralSettings(&dialog);
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("en"));
    QCOMPARE(controller.language(), QStringLiteral("en"));

    controller.setLanguage(QStringLiteral("en")); // redundant echo
    QCoreApplication::processEvents();
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("en"));
}

// Requirement "the dialog must not keep its own copy": with no controller attached, Apply only
// reports intent through the signal and must not write ui/language itself. On the pre-fix code
// this fails, because onSaveGeneralSettings() wrote ui/language straight from the widget state -
// the exact step that used to write the old language back.
void LanguageSyncTest::dialogDoesNotPersistItsOwnLanguageCopy() {
    QSettings().setValue(languageKey, QStringLiteral("zh_CN"));
    SettingsDialog dialog;
    dialog.show();

    selectLanguage(&dialog, QStringLiteral("en"));
    QCOMPARE(selectedLanguage(&dialog), QStringLiteral("en"));
    applyGeneralSettings(&dialog);

    QCOMPARE(QSettings().value(languageKey).toString(), QStringLiteral("zh_CN"));
}

QTEST_MAIN(LanguageSyncTest)

#include "tst_languagesync.moc"
