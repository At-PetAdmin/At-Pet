#include "petcatalog.h"
#include "petwindow.h"
#include "quotepack.h"
#include "settingsdialog.h"
#include "testsettings.h"

#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QtTest>

class BuiltinPetsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        isolateTestSettings();
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("AtPetTests");
        QCoreApplication::setApplicationName("BuiltinPetsTest");
    }
    void resourcesAndQuotes() {
        const auto categories = {QuotePack::Category::Poke, QuotePack::Category::Done,
            QuotePack::Category::Error, QuotePack::Category::Cancel, QuotePack::Category::Idle};
        for (const QString &id : petcatalog::bundledIds()) {
            const QString directory = petcatalog::directory(id);
            QFile config(QDir(directory).filePath("pet.json"));
            QVERIFY(config.open(QIODevice::ReadOnly));
            const auto definition = QJsonDocument::fromJson(config.readAll()).object();
            QCOMPARE(definition.value("name").toString(), petcatalog::name(id));
            QImage image(QDir(directory).filePath(definition.value("sprite").toString()));
            QVERIFY(!image.isNull());
            const QJsonArray cell = definition.value("cell").toArray();
            QCOMPARE(cell.size(), 2);
            QVERIFY(cell.at(0).toInt() > 0 && cell.at(1).toInt() > 0);
            QCOMPARE(image.width() % cell.at(0).toInt(), 0);
            QCOMPARE(image.height() % cell.at(1).toInt(), 0);
            const int frameCount = (image.width() / cell.at(0).toInt()) * (image.height() / cell.at(1).toInt());
            const auto animations = definition.value("animations").toObject();
            for (auto it = animations.begin(); it != animations.end(); ++it) {
                for (const auto frame : it.value().toObject().value("frames").toArray())
                    QVERIFY(frame.toInt(-1) >= 0 && frame.toInt() < frameCount);
            }
            PetWindow pet(directory);
            QCOMPARE(pet.petId(), id);
            QCOMPARE(pet.petName(), petcatalog::name(id));
            QVERIFY(pet.hasAnimation("walkLeft"));
            QVERIFY(pet.hasAnimation("walkRight"));
            const QuotePack pack = QuotePack::load(QDir(directory).filePath("quotes.json"));
            for (const QString &language : {QString("zh_CN"), QString("en")}) {
                for (const auto category : categories) {
                    const auto lines = pack.quotes(category, language);
                    QVERIFY(lines.size() >= 3);
                    QVERIFY(lines.contains(pet.randomQuote(category, language, QuotePack{})));
                }
            }
        }
    }
    void globalQuotesOverrideBundledQuotes() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile source(directory.filePath("global.json"));
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write(R"({"en":{"poke":["custom-poke"],"done":["custom-done"]}})");
        source.close();
        const auto global = QuotePack::load(source.fileName());
        for (const QString &id : petcatalog::bundledIds()) {
            PetWindow pet(petcatalog::directory(id));
            QCOMPARE(pet.randomQuote(QuotePack::Category::Poke, "en", global), QString("custom-poke"));
            QCOMPARE(pet.randomQuote(QuotePack::Category::Done, "en", global), QString("custom-done"));
            QVERIFY(!pet.randomQuote(QuotePack::Category::Idle, "en", global).isEmpty());
        }
    }
    void settingsListAndProtection() {
        SettingsDialog dialog;
        auto *list = dialog.findChild<QListWidget *>();
        QVERIFY(list);
        dialog.findChild<QTabWidget *>()->setCurrentWidget(list->parentWidget());
        for (const QString &id : petcatalog::bundledIds()) {
            QListWidgetItem *found = nullptr;
            for (int i = 0; i < list->count(); ++i) {
                if (list->item(i)->data(Qt::UserRole).toString() == id) found = list->item(i);
            }
            QVERIFY(found);
            list->setCurrentItem(found);
            const auto *preview = dialog.findChild<PetPreviewWidget *>();
            QVERIFY(preview);
            QVERIFY(preview->availableAnimations().contains("walkLeft"));
            for (auto *button : dialog.findChildren<QPushButton *>()) {
                if (button->text().contains("Delete") || button->text().contains("Edit pet.json"))
                    QVERIFY(!button->isEnabled());
            }
            const QString previewDirectory = qEnvironmentVariable("AT_PET_PREVIEW_DIR");
            if (!previewDirectory.isEmpty()) {
                QVERIFY(QDir().mkpath(previewDirectory));
                dialog.show();
                QTest::qWait(50);
                QVERIFY(dialog.grab().save(QDir(previewDirectory).filePath(id + ".png")));
            }
        }
        QVERIFY(petcatalog::directory("../outside").isEmpty());
        QVERIFY(petcatalog::directory("C:/outside").isEmpty());
        QVERIFY(petcatalog::directory("my-pet").startsWith(petcatalog::libraryPath()));
    }
};

QTEST_MAIN(BuiltinPetsTest)
#include "tst_builtinpets.moc"
