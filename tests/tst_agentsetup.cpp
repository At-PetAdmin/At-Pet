#include "agentsetup.h"
#include "agentregistry.h"
#include "settingsdialog.h"
#include "updateswidget.h"
#include "testsettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFontDatabase>
#include <QImageReader>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTranslator>
#include <QTimer>

namespace {
QByteArray read(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
bool write(const QString &path, const QByteArray &data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}
QJsonObject release(const QString &tag, bool preview = false, bool draft = false) {
    return {{"tag_name", tag}, {"prerelease", preview}, {"draft", draft},
        {"html_url", "https://github.com/At-PetAdmin/At-Pet/releases/tag/" + tag}};
}

class ReleaseReply : public QNetworkReply {
public:
    ReleaseReply(const QNetworkRequest &request, QByteArray data, bool error, QObject *parent)
        : QNetworkReply(parent), data_(std::move(data)) {
        setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly);
        if (error) setError(QNetworkReply::ContentAccessDenied, "Rate limit exceeded");
        QTimer::singleShot(0, this, [this] { setFinished(true); emit readyRead(); emit finished(); });
    }
    void abort() override {}
    qint64 bytesAvailable() const override { return data_.size() - offset_ + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *buffer, qint64 maxSize) override {
        const auto count = qMin(maxSize, qint64(data_.size()) - offset_);
        if (count <= 0) return -1;
        memcpy(buffer, data_.constData() + offset_, size_t(count)); offset_ += count; return count;
    }
private:
    QByteArray data_;
    qint64 offset_ = 0;
};
class ReleaseNetwork : public QNetworkAccessManager {
public:
    QList<QByteArray> pages;
    QList<QUrl> requests;
    bool fail = false;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        requests << request.url();
        return new ReleaseReply(request, pages.isEmpty() ? QByteArray("[]") : pages.takeFirst(), fail, this);
    }
};
}

class AgentSetupTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { isolateTestSettings(); QStandardPaths::setTestModeEnabled(true); }
    void init() { QSettings().clear(); }

    void preservesExistingHooksAndSettingsAndIsIdempotent() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = agentsetup::configPath("claudecode", dir.path());
        const QByteArray original = R"({"theme":"dark","hooks":{"Stop":[{"matcher":"*","hooks":[{"type":"command","command":"echo user-hook"}]}],"CustomEvent":[{"anything":true}]}})";
        QVERIFY(write(path, original));
        const auto result = agentsetup::configure("claudecode", dir.path(), dir.filePath("runtime with spaces"), 9234);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(!result.backup.isEmpty());
        QCOMPARE(read(result.backup), original);
        auto config = QJsonDocument::fromJson(read(path)).object();
        QCOMPARE(config.value("theme").toString(), "dark");
        const auto hooks = config.value("hooks").toObject();
        QCOMPARE(hooks.value("Stop").toArray().size(), 2);
        QVERIFY(hooks.contains("CustomEvent"));
        const auto before = read(path);
        const auto second = agentsetup::configure("claudecode", dir.path(), dir.filePath("runtime with spaces"), 9234);
        QVERIFY(second.ok);
        QVERIFY(!second.changed);
        QVERIFY(second.backup.isEmpty());
        QCOMPARE(read(path), before);
        const QString launcher = dir.filePath("runtime with spaces/agent-bridge/at-pet-claude-code.mjs");
        QVERIFY(read(launcher).contains("9234"));
        QVERIFY(!read(path).contains("Bearer"));
        QVERIFY(QFile::exists(dir.filePath("runtime with spaces/agent-bridge/client-auth.mjs")));
        QVERIFY(QFile::exists(dir.filePath("runtime with spaces/agent-bridge/profiles/_claude-hook-family.json")));
        // Run the real installed launcher with harmless unmapped input. It must return even
        // when At-Pet is unavailable, and paths with spaces must work.
        const QString node = QStandardPaths::findExecutable("node");
        if (!node.isEmpty()) {
            QProcess process;
            process.start(node, {launcher});
            QVERIFY(process.waitForStarted());
            process.write("{}\n"); process.closeWriteChannel();
            QVERIFY(process.waitForFinished(10000));
            QCOMPARE(process.exitCode(), 0);
            QVERIFY2(process.readAllStandardError().isEmpty(), "Installed bridge failed");
        }
    }

    void rejectsMalformedConfigurationWithoutChangingIt_data() {
        QTest::addColumn<QByteArray>("input");
        QTest::newRow("broken JSON") << QByteArray("{broken");
        QTest::newRow("array root") << QByteArray("[]");
        QTest::newRow("bad hooks") << QByteArray("{\"hooks\":[]}");
        QTest::newRow("bad event") << QByteArray("{\"hooks\":{\"Stop\":{}}}");
    }
    void rejectsMalformedConfigurationWithoutChangingIt() {
        QFETCH(QByteArray, input);
        QTemporaryDir dir;
        const auto path = agentsetup::configPath("openaicodex", dir.path());
        QVERIFY(write(path, input));
        const auto result = agentsetup::configure("openaicodex", dir.path(), dir.filePath("runtime"), 8765);
        QVERIFY(!result.ok);
        QCOMPARE(read(path), input);
        QVERIFY(!QDir(dir.filePath("runtime")).exists());
    }

    void installsEachSupportedAgentAndUpdatesPortWithoutDuplicateHooks() {
        QTemporaryDir dir;
        for (const auto &agent : agentDefinitions()) {
            if (!agentsetup::canConfigure(agent.key)) continue;
            const auto first = agentsetup::configure(agent.key, dir.path(), dir.filePath("runtime"), 8765);
            QVERIFY2(first.ok, qPrintable(agent.key + ": " + first.error));
            const auto second = agentsetup::configure(agent.key, dir.path(), dir.filePath("runtime"), 9123);
            QVERIFY(second.ok);
            if (agent.key == "opencode") {
                QVERIFY(read(first.path).contains("9123"));
                QVERIFY(QFile::exists(QFileInfo(first.path).dir().filePath("client-auth.mjs")));
            } else {
                const auto hooks = QJsonDocument::fromJson(read(first.path)).object().value("hooks").toObject();
                QCOMPARE(hooks.value("Stop").toArray().size(), 1);
            }
        }
        QVERIFY(!agentsetup::configure("antigravity", dir.path(), dir.filePath("runtime"), 8765).ok);
    }

    void organizationGuidesUseDedicatedRepositories() {
        QVERIFY(agentsetup::guideUrl("openaicodex").toString().contains("At-PetAdmin/codex-at-pet"));
        QVERIFY(agentsetup::guideText("openaicodex").contains("codex plugin add codex-at-pet@at-pet-local"));
        QVERIFY(agentsetup::guideUrl("deepseekharness").toString().contains("At-PetAdmin/dsh_at-pet"));
        QVERIFY(agentsetup::guideText("deepseekharness").contains("install_bundle"));
        QVERIFY(agentsetup::guideUrl("githubcopilot").toString().contains("At-PetAdmin/github-copilot_at-pet"));
    }

    void releaseSelectionSeparatesChannelsAndComparesVersions() {
        const QJsonArray releases{release("v1.3.4-beta2", true), release("v1.3.3"), release("v1.3.4-beta10", true),
            release("v9.0.0", false, true), release("v1.3.4beta1", true), release("nightly", true)};
        QCOMPARE(updates::latestRelease(releases, false).value("tag_name").toString(), "v1.3.3");
        QCOMPARE(updates::latestRelease(releases, true).value("tag_name").toString(), "v1.3.4-beta10");
        QVERIFY(updates::compareVersions("v1.3.4", "1.3.4-beta10") > 0);
        QVERIFY(updates::compareVersions("v1.3.4-beta10", "1.3.4-beta2") > 0);
        QVERIFY(updates::compareVersions("v1.3.4-beta1", "1.3.3") > 0);
        QVERIFY(updates::latestRelease({release("v1.3.4beta1")}, false).isEmpty());
    }

    void downloadsOnlyMatchingArchitectureAndOfficialAssets() {
        auto item = release("v1.3.4");
        const QString url = "https://github.com/At-PetAdmin/At-Pet/releases/download/v1.3.4/At-Pet-windows-arm64.zip";
        item.insert("assets", QJsonArray{QJsonObject{{"name", "At-Pet-windows-arm64.zip.sha256"}, {"browser_download_url", "https://other.test/a"}},
            QJsonObject{{"name", "At-Pet-windows-arm64.zip"}, {"browser_download_url", url}}});
        QCOMPARE(updates::assetUrl(item, "arm64"), QUrl(url));
        QVERIFY(updates::assetUrl(item, "amd64").isEmpty());
        QVERIFY(updates::assetUrl(item, "arm").isEmpty());
    }

    void searchKeepsSelectionAndUpdatesPageExists() {
        SettingsDialog dialog;
        const auto search = dialog.findChild<QLineEdit *>("agentSearch");
        const auto codex = dialog.findChild<QCheckBox *>("agentEnabled_openaicodex");
        const auto claude = dialog.findChild<QCheckBox *>("agentEnabled_claudecode");
        QVERIFY(search && codex && claude);
        const bool selected = claude->isChecked();
        search->setText("codex");
        QVERIFY(!codex->parentWidget()->isHidden());
        QVERIFY(claude->parentWidget()->isHidden());
        QCOMPARE(claude->isChecked(), selected);
        search->clear();
        QVERIFY(!claude->parentWidget()->isHidden());
        QVERIFY(dialog.findChild<UpdatesWidget *>());
        QVERIFY(dialog.findChild<QPushButton *>("checkUpdatesButton")->isEnabled());
    }

    void optionalVisualAndLiveUpdateCheck() {
        const QString folder = qEnvironmentVariable("AT_PET_UI_CAPTURE_DIR");
        if (folder.isEmpty()) QSKIP("Set AT_PET_UI_CAPTURE_DIR for manual visual and live-network verification.");
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
        QVERIFY(!QImage(":/integrations/check-light.xpm").isNull());
        QTranslator translator;
        QVERIFY(translator.load(qEnvironmentVariable("AT_PET_TRANSLATION")));
        qApp->installTranslator(&translator);
        QSettings().setValue("ui/theme", "light");
        SettingsDialog dialog;
        auto *tabs = dialog.findChild<QTabWidget *>();
        tabs->setCurrentIndex(1);
        dialog.show();
        QTest::qWait(300);
        QVERIFY(dialog.grab().save(QDir(folder).filePath("agent-setup-light.png")));
        tabs->setCurrentIndex(4);
        dialog.findChild<QPushButton *>("checkUpdatesButton")->click();
        QTRY_VERIFY_WITH_TIMEOUT(dialog.findChild<QPushButton *>("checkUpdatesButton")->isEnabled(), 45000);
        qInfo().noquote() << "Live GitHub update result:" << dialog.findChild<QLabel *>("updateStatus")->text()
            << dialog.findChild<QLabel *>("updateStatus")->toolTip();
        QVERIFY(dialog.grab().save(QDir(folder).filePath("updates-light.png")));
        QSettings().setValue("ui/theme", "dark");
        SettingsDialog dark;
        dark.findChild<QTabWidget *>()->setCurrentIndex(1);
        dark.show();
        QTest::qWait(300);
        QVERIFY(dark.grab().save(QDir(folder).filePath("agent-setup-dark.png")));
        qApp->removeTranslator(&translator);
    }

    void updateUiHandlesPaginationChannelChangesAndNetworkRecovery() {
        ReleaseNetwork network;
        QJsonArray fullPage;
        for (int i = 0; i < 100; ++i) fullPage.append(release("v1.0.0"));
        auto latest = release("v1.3.4-beta10", true);
        latest.insert("body", "Beta notes from GitHub");
        latest.insert("assets", QJsonArray{QJsonObject{{"name", "At-Pet-windows-amd64.zip"},
            {"browser_download_url", "https://github.com/At-PetAdmin/At-Pet/releases/download/v1.3.4-beta10/At-Pet-windows-amd64.zip"}}});
        network.pages = {QJsonDocument(fullPage).toJson(), QJsonDocument(QJsonArray{release("v1.3.3"), latest}).toJson()};
        UpdatesWidget widget(nullptr, &network);
        auto *button = widget.findChild<QPushButton *>("checkUpdatesButton");
        button->click();
        QVERIFY(!button->isEnabled());
        QTRY_VERIFY(button->isEnabled());
        QCOMPARE(network.requests.size(), 2);
        QVERIFY(network.requests.last().query().contains("page=2"));
        auto *channel = widget.findChild<QComboBox *>("updateChannel");
        channel->setCurrentIndex(1);
        QCOMPARE(widget.findChild<QPlainTextEdit *>()->toPlainText(), "Beta notes from GitHub");
        QVERIFY(widget.findChild<QLabel *>("updateStatus")->text().contains("v1.3.4-beta10"));
        channel->setCurrentIndex(0);
        QVERIFY(widget.findChild<QLabel *>("updateStatus")->text().contains("v1.3.3"));
        network.fail = true;
        button->click();
        QTRY_VERIFY(button->isEnabled());
        QVERIFY(widget.findChild<QLabel *>("updateStatus")->text().contains("Could not check updates"));
        QVERIFY(!widget.findChild<QPushButton *>("primaryBtn")->isEnabled());
        network.fail = false;
        network.pages = {QByteArray("invalid JSON")};
        button->click(); QTRY_VERIFY(button->isEnabled());
        QVERIFY(widget.findChild<QLabel *>("updateStatus")->text().contains("Invalid release response"));
        network.pages = {QJsonDocument(QJsonArray{release("v1.3.4")}).toJson()};
        button->click(); QTRY_VERIFY(button->isEnabled());
        QVERIFY(widget.findChild<QLabel *>("updateStatus")->text().contains("v1.3.4"));
    }
};

QTEST_MAIN(AgentSetupTest)
#include "tst_agentsetup.moc"
