// Regression test for "skip animations a pet does not configure".
//
// PetWindow::setAnimation() used to fall back to the built-in vector animation names, so a sprite
// pet that only ships some animations switched to an unconfigured one and painted nothing. The
// contract now is:
//   1. A pet.json with a non-empty "animations" object may only play those entries.
//   2. A pet without an animation table (built-in vector pet, or legacy pet.json) keeps using the
//      built-in names.
//   3. Skipping an animation leaves the current one on screen and emits nothing.
//   4. A hot reload that removes the current animation falls back to idle, or to the first
//      configured animation when there is no idle.
//   5. withInferredAnimations() (row_counts -> animations) keeps working.

#include "petwindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

namespace {

QJsonObject frameAnimation(int row = 0, int frames = 1) {
    return QJsonObject{{QStringLiteral("row"), row}, {QStringLiteral("frames"), frames}};
}

QJsonObject animations(const QStringList &names) {
    QJsonObject table;
    int row = 0;
    for (const QString &name : names)
        table.insert(name, frameAnimation(row++));
    return table;
}

QString writePetJson(const QString &directory, const QJsonObject &definition) {
    const QString path = QDir(directory).filePath(QStringLiteral("pet.json"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(QJsonDocument(definition).toJson());
    file.close();
    return path;
}

} // namespace

class AnimationFallbackTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void unconfiguredAnimationIsSkipped();
    void configuredAnimationCanPlay();
    void reloadFallsBackToIdleWhenCurrentAnimationIsRemoved();
    void reloadFallsBackToFirstConfiguredAnimationWithoutIdle();
    void petWithoutAnimationTableKeepsBuiltinNames();
    void inferredRowCountsStillProvideAnimations();

private:
    QTemporaryDir *directory_ = nullptr;
};

void AnimationFallbackTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("At-Pet-Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("AnimationFallbackTest"));
}

void AnimationFallbackTest::init() {
    directory_ = new QTemporaryDir;
    QVERIFY(directory_->isValid());
}

void AnimationFallbackTest::cleanup() {
    delete directory_;
    directory_ = nullptr;
}

// The bug: "done" is not configured, so the sprite has no cell to paint. The animation must stay
// on idle and the skipped switch must not announce itself.
void AnimationFallbackTest::unconfiguredAnimationIsSkipped() {
    writePetJson(directory_->path(), QJsonObject{
        {QStringLiteral("name"), QStringLiteral("OnlyIdle")},
        {QStringLiteral("animations"), animations({QStringLiteral("idle")})}});
    PetWindow pet(directory_->path());
    QCOMPARE(pet.animation(), QStringLiteral("idle"));
    QVERIFY(pet.hasAnimation(QStringLiteral("idle")));
    QVERIFY(!pet.hasAnimation(QStringLiteral("done")));
    QVERIFY(!pet.hasAnimation(QString()));

    QSignalSpy spy(&pet, &PetWindow::animationChanged);
    pet.setAnimation(QStringLiteral("done"));
    QCOMPARE(pet.animation(), QStringLiteral("idle"));
    QCOMPARE(spy.count(), 0);

    pet.setAnimation(QStringLiteral("bogus"));
    QCOMPARE(pet.animation(), QStringLiteral("idle"));
}

void AnimationFallbackTest::configuredAnimationCanPlay() {
    writePetJson(directory_->path(), QJsonObject{
        {QStringLiteral("name"), QStringLiteral("IdleOnly")},
        {QStringLiteral("animations"), animations({QStringLiteral("idle")})}});
    PetWindow pet(directory_->path());
    QVERIFY(!pet.hasAnimation(QStringLiteral("done")));

    writePetJson(directory_->path(), QJsonObject{
        {QStringLiteral("name"), QStringLiteral("IdleAndDone")},
        {QStringLiteral("animations"), animations({QStringLiteral("idle"), QStringLiteral("done")})}});
    pet.reloadDefinition();

    QVERIFY(pet.hasAnimation(QStringLiteral("done")));
    pet.setAnimation(QStringLiteral("done"));
    QCOMPARE(pet.animation(), QStringLiteral("done"));
}

// Hot reload while a removed animation is on screen: idle is still configured, so use it.
void AnimationFallbackTest::reloadFallsBackToIdleWhenCurrentAnimationIsRemoved() {
    writePetJson(directory_->path(), QJsonObject{
        {QStringLiteral("name"), QStringLiteral("IdleAndDone")},
        {QStringLiteral("animations"), animations({QStringLiteral("idle"), QStringLiteral("done")})}});
    PetWindow pet(directory_->path());
    pet.setAnimation(QStringLiteral("done"));
    QCOMPARE(pet.animation(), QStringLiteral("done"));

    writePetJson(directory_->path(), QJsonObject{
        {QStringLiteral("name"), QStringLiteral("IdleOnly")},
        {QStringLiteral("animations"), animations({QStringLiteral("idle")})}});
    pet.reloadDefinition();

    QCOMPARE(pet.animation(), QStringLiteral("idle"));
    QVERIFY(!pet.hasAnimation(QStringLiteral("done")));
}

// Same hot reload, but the new table has no idle either: the first configured animation wins
// instead of leaving the pet on a cell that no longer exists.
void AnimationFallbackTest::reloadFallsBackToFirstConfiguredAnimationWithoutIdle() {
    writePetJson(directory_->path(), QJsonObject{
        {QStringLiteral("name"), QStringLiteral("SleepAndWork")},
        {QStringLiteral("animations"), animations({QStringLiteral("sleep"), QStringLiteral("work")})}});
    PetWindow pet(directory_->path());
    // No idle in this table: the constructor falls back to the first configured entry.
    QCOMPARE(pet.animation(), QStringLiteral("sleep"));

    pet.setAnimation(QStringLiteral("work"));
    QCOMPARE(pet.animation(), QStringLiteral("work"));

    // Hot reload drops "work" and still has no idle, so the first configured entry returns.
    writePetJson(directory_->path(), QJsonObject{
        {QStringLiteral("name"), QStringLiteral("SleepOnly")},
        {QStringLiteral("animations"), animations({QStringLiteral("sleep")})}});
    pet.reloadDefinition();

    QCOMPARE(pet.animation(), QStringLiteral("sleep"));
    QVERIFY(!pet.hasAnimation(QStringLiteral("work")));
}

// No animation table at all means the built-in vector pet (or a legacy pet.json): the built-in
// names stay available.
void AnimationFallbackTest::petWithoutAnimationTableKeepsBuiltinNames() {
    // No pet.json in this directory, so reloadDefinition() keeps the empty definition.
    PetWindow pet(directory_->path());
    QVERIFY(pet.hasAnimation(QStringLiteral("idle")));
    QVERIFY(pet.hasAnimation(QStringLiteral("done")));
    QVERIFY(pet.hasAnimation(QStringLiteral("thinking")));
    QVERIFY(!pet.hasAnimation(QStringLiteral("bogus")));

    pet.setAnimation(QStringLiteral("done"));
    QCOMPARE(pet.animation(), QStringLiteral("done"));
}

// withInferredAnimations() turns row_counts into named animations, so a documented legacy
// pet.json keeps working and only the inferred entries are playable.
void AnimationFallbackTest::inferredRowCountsStillProvideAnimations() {
    writePetJson(directory_->path(), QJsonObject{
        {QStringLiteral("name"), QStringLiteral("RowCounts")},
        {QStringLiteral("row_counts"), QJsonArray{2, 3}}});
    PetWindow pet(directory_->path());
    QVERIFY(pet.hasAnimation(QStringLiteral("idle")));
    QVERIFY(pet.hasAnimation(QStringLiteral("walk")));
    QVERIFY(!pet.hasAnimation(QStringLiteral("done")));

    pet.setAnimation(QStringLiteral("walk"));
    QCOMPARE(pet.animation(), QStringLiteral("walk"));
}

QTEST_MAIN(AnimationFallbackTest)

#include "tst_animationfallback.moc"
