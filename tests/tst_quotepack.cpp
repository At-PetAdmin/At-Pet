// Tests for the shared quote pack (app/quotepack.cpp).
//
// The pack is the file the Settings dialog imports and PetController reads at runtime:
// QStandardPaths::AppDataLocation/quotes.json, grouped by language and then by category
// (poke / done / error / cancel / idle). The suite covers the tolerant sentence formats, the
// category aliases, the language fallback chain, the random draw and the atomic install.
//
// Everything runs in QStandardPaths test mode with its own organization/application name so
// the developer's real At-Pet data directory is never read or written.

#include "quotepack.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

namespace {

const char *const twoLanguagePack = R"json({
    "zh_CN": {
        "poke": ["别戳我啦～", "痒痒的！"],
        "done": ["完工啦，检查一下？"],
        "error": ["出错了，我来看看日志。"],
        "cancel": ["已取消，随时再叫我。"],
        "idle": ["要不要休息一下？"]
    },
    "en": {
        "poke": ["Hey, that tickles!"],
        "done": ["All done! Want to review?"],
        "error": ["Something broke — check the logs?"],
        "cancel": ["Cancelled. Ping me anytime."],
        "idle": ["Take a short break?", "Right here with you."]
    }
})json";

QStringList sorted(QStringList values) {
    values.sort();
    return values;
}

QString writeTextFile(const QString &path, const QByteArray &contents) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(contents);
    file.close();
    return path;
}

QByteArray readFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

class QuotePackTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void defaultPathEndsWithQuotesJson();
    void missingFileLoadsEmpty();
    void loadsEverySentenceFormat();
    void categoryAliasesMergeIntoKnownCategories();
    void languageFallbackChain();
    void languageKeysAreNormalized();
    void blankAndNonStringEntriesAreDropped();
    void duplicateLinesAreRemoved();
    void invalidJsonReportsError();
    void nonObjectDocumentReportsError();
    void packWithoutQuotesReportsError();
    void randomQuoteStaysInsideTheCategory();
    void installCopiesAndOverwrites();
    void installRejectsInvalidSourceWithoutTouchingExisting();
    void installMissingSourceFails();
    void fileOrderDrivesLastResortFallback();

private:
    QTemporaryDir *directory_ = nullptr;
};

void QuotePackTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("At-Pet-Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("QuotePackTest"));
}

void QuotePackTest::init() {
    directory_ = new QTemporaryDir;
    QVERIFY(directory_->isValid());
}

void QuotePackTest::cleanup() {
    delete directory_;
    directory_ = nullptr;
    QFile::remove(QuotePack::defaultPath());
}

void QuotePackTest::defaultPathEndsWithQuotesJson() {
    const QString path = QuotePack::defaultPath();
    QVERIFY(path.endsWith(QStringLiteral("quotes.json")));
    QVERIFY(path.startsWith(QDir::fromNativeSeparators(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))));
}

// No pack installed is the normal first-run state: not an error, just an empty pack.
void QuotePackTest::missingFileLoadsEmpty() {
    QString error = QStringLiteral("untouched");
    const QuotePack pack =
        QuotePack::load(directory_->filePath(QStringLiteral("absent.json")), &error);
    QVERIFY(pack.isEmpty());
    QVERIFY(error.isEmpty());
    QVERIFY(!pack.hasCategory(QuotePack::Category::Poke));
    QVERIFY(pack.randomQuote(QuotePack::Category::Poke, QStringLiteral("en")).isEmpty());
}

// The pet.json quotes field tolerates a bare string, an array of strings and objects with a
// "text" field. The pack accepts the same three shapes per category.
void QuotePackTest::loadsEverySentenceFormat() {
    const QString path = writeTextFile(directory_->filePath(QStringLiteral("formats.json")),
                                       R"json({
        "en": {
            "poke": "plain line",
            "done": ["first", "second"],
            "error": [{"text": "object entry"}, {"ignored": true}, 42, null],
            "cancel": {"text": "single object"},
            "idle": "line one\nline two"
        }
    })json");
    const QuotePack pack = QuotePack::load(path);
    QVERIFY(!pack.isEmpty());
    QCOMPARE(pack.languages(), QStringList{QStringLiteral("en")});
    QCOMPARE(pack.quotes(QuotePack::Category::Poke, QStringLiteral("en")),
             QStringList{QStringLiteral("plain line")});
    QCOMPARE(pack.quotes(QuotePack::Category::Done, QStringLiteral("en")),
             (QStringList{QStringLiteral("first"), QStringLiteral("second")}));
    QCOMPARE(pack.quotes(QuotePack::Category::Error, QStringLiteral("en")),
             QStringList{QStringLiteral("object entry")});
    QCOMPARE(pack.quotes(QuotePack::Category::Cancel, QStringLiteral("en")),
             QStringList{QStringLiteral("single object")});
    QCOMPARE(pack.quotes(QuotePack::Category::Idle, QStringLiteral("en")),
             (QStringList{QStringLiteral("line one"), QStringLiteral("line two")}));
}

void QuotePackTest::categoryAliasesMergeIntoKnownCategories() {
    const QString path = writeTextFile(directory_->filePath(QStringLiteral("aliases.json")),
                                       R"json({
        "en": {
            "poked": ["p1"], "click": ["p2"],
            "completion": ["d1"], "finished": ["d2"],
            "failed": ["e1"], "failure": ["e2"],
            "cancelled": ["c1"], "aborted": ["c2"],
            "waiting": ["i1"], "random": ["i2"]
        }
    })json");
    const QuotePack pack = QuotePack::load(path);
    QVERIFY(!pack.isEmpty());
    for (const auto category : {QuotePack::Category::Poke, QuotePack::Category::Done,
                                QuotePack::Category::Error, QuotePack::Category::Cancel,
                                QuotePack::Category::Idle}) {
        QVERIFY(pack.hasCategory(category));
        QCOMPARE(int(pack.quotes(category, QStringLiteral("en")).size()), 2);
    }
    QCOMPARE(sorted(pack.quotes(QuotePack::Category::Poke, QStringLiteral("en"))),
             (QStringList{QStringLiteral("p1"), QStringLiteral("p2")}));
    QCOMPARE(sorted(pack.quotes(QuotePack::Category::Cancel, QStringLiteral("en"))),
             (QStringList{QStringLiteral("c1"), QStringLiteral("c2")}));
}

// exact language -> base language -> en -> first language of the file that has the category.
void QuotePackTest::languageFallbackChain() {
    const QString path = writeTextFile(directory_->filePath(QStringLiteral("fallback.json")),
                                       R"json({
        "zh_CN": {"poke": ["cn-exact"]},
        "zh": {"poke": ["zh-base"], "done": ["zh-done"]},
        "en": {"poke": ["en-poke"], "error": ["en-error"]},
        "de": {"poke": ["de-poke"], "idle": ["de-idle"]}
    })json");
    const QuotePack pack = QuotePack::load(path);
    QVERIFY(!pack.isEmpty());

    QCOMPARE(pack.quotes(QuotePack::Category::Poke, QStringLiteral("zh_CN")),
             QStringList{QStringLiteral("cn-exact")});
    QCOMPARE(pack.quotes(QuotePack::Category::Done, QStringLiteral("zh_CN")),
             QStringList{QStringLiteral("zh-done")});
    QCOMPARE(pack.quotes(QuotePack::Category::Error, QStringLiteral("zh_CN")),
             QStringList{QStringLiteral("en-error")});
    QCOMPARE(pack.quotes(QuotePack::Category::Idle, QStringLiteral("zh_CN")),
             QStringList{QStringLiteral("de-idle")});
    QCOMPARE(pack.quotes(QuotePack::Category::Poke, QStringLiteral("fr")),
             QStringList{QStringLiteral("en-poke")});
    QCOMPARE(pack.quotes(QuotePack::Category::Poke, QString()), QStringList{QStringLiteral("en-poke")});
}

// zh_CN -> zh_Hans and en -> en_GB regional variants, plus case/-/_ normalisation.
void QuotePackTest::languageKeysAreNormalized() {
    const QString regional = writeTextFile(directory_->filePath(QStringLiteral("regional.json")),
                                          R"json({
        "zh_Hans": {"poke": ["hans"]},
        "en_GB": {"poke": ["gb"]}
    })json");
    const QuotePack pack = QuotePack::load(regional);
    QCOMPARE(pack.quotes(QuotePack::Category::Poke, QStringLiteral("zh_CN")),
             QStringList{QStringLiteral("hans")});
    QCOMPARE(pack.quotes(QuotePack::Category::Poke, QStringLiteral("en_US")),
             QStringList{QStringLiteral("gb")});

    const QString mixedCase = writeTextFile(directory_->filePath(QStringLiteral("mixed.json")),
                                            R"json({"ZH-cn": {"poke": ["cn"]}, "EN": {"done": ["en"]}})json");
    const QuotePack mixed = QuotePack::load(mixedCase);
    QCOMPARE(mixed.languages(), (QStringList{QStringLiteral("zh_cn"), QStringLiteral("en")}));
    QCOMPARE(mixed.quotes(QuotePack::Category::Poke, QStringLiteral("zh_CN")),
             QStringList{QStringLiteral("cn")});
    QCOMPARE(mixed.quotes(QuotePack::Category::Done, QStringLiteral("en_US")),
             QStringList{QStringLiteral("en")});
}

void QuotePackTest::blankAndNonStringEntriesAreDropped() {
    const QString path = writeTextFile(directory_->filePath(QStringLiteral("blank.json")),
                                       R"json({
        "en": {
            "poke": ["", "   ", "\n", "kept", "  kept  "],
            "done": [{"text": "  "}, {"text": ""}, {"text": "real"}],
            "error": [],
            "cancel": 42,
            "idle": null
        }
    })json");
    const QuotePack pack = QuotePack::load(path);
    QVERIFY(!pack.isEmpty());
    QCOMPARE(pack.quotes(QuotePack::Category::Poke, QStringLiteral("en")),
             QStringList{QStringLiteral("kept")});
    QCOMPARE(pack.quotes(QuotePack::Category::Done, QStringLiteral("en")),
             QStringList{QStringLiteral("real")});
    QVERIFY(!pack.hasCategory(QuotePack::Category::Error));
    QVERIFY(!pack.hasCategory(QuotePack::Category::Cancel));
    QVERIFY(!pack.hasCategory(QuotePack::Category::Idle));
    QCOMPARE(pack.quoteCount(), 2);
}

void QuotePackTest::duplicateLinesAreRemoved() {
    const QString path = writeTextFile(directory_->filePath(QStringLiteral("dupes.json")),
                                       R"json({"en": {"poke": ["same", "same ", "other"]}})json");
    const QuotePack pack = QuotePack::load(path);
    QCOMPARE(pack.quotes(QuotePack::Category::Poke, QStringLiteral("en")),
             (QStringList{QStringLiteral("same"), QStringLiteral("other")}));
}

void QuotePackTest::invalidJsonReportsError() {
    const QString path = writeTextFile(directory_->filePath(QStringLiteral("broken.json")),
                                       R"json({"en": {"poke": [})json");
    QString error;
    const QuotePack pack = QuotePack::load(path, &error);
    QVERIFY(pack.isEmpty());
    QVERIFY(!error.isEmpty());
}

void QuotePackTest::nonObjectDocumentReportsError() {
    const QString path = writeTextFile(directory_->filePath(QStringLiteral("array.json")),
                                       R"json([{"en": {"poke": ["x"]}}])json");
    QString error;
    const QuotePack pack = QuotePack::load(path, &error);
    QVERIFY(pack.isEmpty());
    QVERIFY(!error.isEmpty());
}

// A syntactically valid file without a single usable line counts as a validation failure, so the
// importer refuses it instead of replacing a good pack with an empty one.
void QuotePackTest::packWithoutQuotesReportsError() {
    for (const QByteArray &contents : {QByteArray("{}"), QByteArray(R"json({"en": {}})json"),
                                       QByteArray(R"json({"en": {"poke": []}})json"),
                                       QByteArray(R"json({"en": {"unknown": ["x"]}})json")}) {
        const QString path = writeTextFile(directory_->filePath(QStringLiteral("empty.json")), contents);
        QString error;
        const QuotePack pack = QuotePack::load(path, &error);
        QVERIFY(pack.isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(!pack.hasCategory(QuotePack::Category::Poke));
    }
}

void QuotePackTest::randomQuoteStaysInsideTheCategory() {
    const QString path = writeTextFile(directory_->filePath(QStringLiteral("random.json")),
                                       twoLanguagePack);
    const QuotePack pack = QuotePack::load(path);
    QVERIFY(!pack.isEmpty());
    const QStringList english = pack.quotes(QuotePack::Category::Poke, QStringLiteral("en"));
    QCOMPARE(int(english.size()), 1);

    const QStringList chinese = pack.quotes(QuotePack::Category::Poke, QStringLiteral("zh_CN"));
    const QSet<QString> allowed(chinese.cbegin(), chinese.cend());
    for (int i = 0; i < 80; ++i) {
        const QString quote = pack.randomQuote(QuotePack::Category::Poke, QStringLiteral("zh_CN"));
        QVERIFY(allowed.contains(quote));
    }
    // The "en" category has a single line, so the random draw is deterministic there.
    QCOMPARE(pack.randomQuote(QuotePack::Category::Done, QStringLiteral("zh_CN")),
             QStringLiteral("完工啦，检查一下？"));
}

void QuotePackTest::installCopiesAndOverwrites() {
    const QString source = writeTextFile(directory_->filePath(QStringLiteral("source.json")),
                                         twoLanguagePack);
    QString error;
    QVERIFY2(QuotePack::install(source, &error), qPrintable(error));

    const QString installed = QuotePack::defaultPath();
    QVERIFY(QFile::exists(installed));
    QCOMPARE(readFile(installed), readFile(source));
    QuotePack pack = QuotePack::load(installed);
    QCOMPARE(pack.languageCount(), 2);
    // Language keys are normalised (lowercase, '-' becomes '_'), so lookups stay case-insensitive.
    QCOMPARE(pack.languages(), (QStringList{QStringLiteral("zh_cn"), QStringLiteral("en")}));
    QCOMPARE(pack.quotes(QuotePack::Category::Done, QStringLiteral("en")),
             QStringList{QStringLiteral("All done! Want to review?")});

    const QString replacement = writeTextFile(directory_->filePath(QStringLiteral("replacement.json")),
                                              R"json({"fr": {"poke": ["bonjour"], "done": ["fini"]}})json");
    QVERIFY2(QuotePack::install(replacement, &error), qPrintable(error));
    QCOMPARE(readFile(installed), readFile(replacement));
    pack = QuotePack::load(installed);
    QCOMPARE(pack.languages(), QStringList{QStringLiteral("fr")});
    QCOMPARE(pack.quotes(QuotePack::Category::Poke, QStringLiteral("fr")),
             QStringList{QStringLiteral("bonjour")});
}

void QuotePackTest::installRejectsInvalidSourceWithoutTouchingExisting() {
    const QString source = writeTextFile(directory_->filePath(QStringLiteral("source.json")),
                                         twoLanguagePack);
    QString error;
    QVERIFY2(QuotePack::install(source, &error), qPrintable(error));
    const QByteArray before = readFile(QuotePack::defaultPath());

    const QString invalid = writeTextFile(directory_->filePath(QStringLiteral("invalid.json")),
                                          R"json({"en": {"poke": [}})json");
    error.clear();
    QVERIFY(!QuotePack::install(invalid, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(readFile(QuotePack::defaultPath()), before);

    const QString noQuotes = writeTextFile(directory_->filePath(QStringLiteral("noquotes.json")),
                                           R"json({"en": {}})json");
    error.clear();
    QVERIFY(!QuotePack::install(noQuotes, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(readFile(QuotePack::defaultPath()), before);
}

void QuotePackTest::installMissingSourceFails() {
    QString error;
    QVERIFY(!QuotePack::install(directory_->filePath(QStringLiteral("absent.json")), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFile::exists(QuotePack::defaultPath()));
}

// QJsonObject keeps its keys sorted, so the last-resort fallback must use the on-disk order:
// "fr" appears first although "de" sorts first alphabetically.
void QuotePackTest::fileOrderDrivesLastResortFallback() {
    const QString path = writeTextFile(directory_->filePath(QStringLiteral("order.json")),
                                       R"json({"fr": {"poke": ["fr-line"]}, "de": {"poke": ["de-line"]}})json");
    const QuotePack pack = QuotePack::load(path);
    QCOMPARE(pack.languages(), (QStringList{QStringLiteral("fr"), QStringLiteral("de")}));
    QCOMPARE(pack.quotes(QuotePack::Category::Poke, QStringLiteral("ja")),
             QStringList{QStringLiteral("fr-line")});
}

QTEST_MAIN(QuotePackTest)

#include "tst_quotepack.moc"
