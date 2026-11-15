// Regression test for directional walk mirroring.
//
// PetWindow used to mirror every sprite while walking left, even when the pet configured a
// dedicated walkLeft animation. Left-facing art was therefore flipped back to face right.
// The contract now is:
//   1. A shared "walk" fallback is mirrored when the pet moves left, so it must face right.
//   2. walkLeft/walkRight art is drawn as authored, never mirrored.

#include "petwindow.h"

#include <QTest>

class WalkMirrorTest final : public QObject {
    Q_OBJECT

private slots:
    void sharedWalkMirrorsWhenMovingLeft();
    void directionalArtIsNotMirrored();
    void idleDoesNotMirror();
};

void WalkMirrorTest::sharedWalkMirrorsWhenMovingLeft() {
    QVERIFY(PetWindow::spriteShouldMirror(true, true, false));
    QVERIFY(!PetWindow::spriteShouldMirror(true, false, false));
}

void WalkMirrorTest::directionalArtIsNotMirrored() {
    QVERIFY(!PetWindow::spriteShouldMirror(true, true, true));
    QVERIFY(!PetWindow::spriteShouldMirror(true, false, true));
}

void WalkMirrorTest::idleDoesNotMirror() {
    QVERIFY(!PetWindow::spriteShouldMirror(false, false, false));
    QVERIFY(!PetWindow::spriteShouldMirror(false, true, false));
}

QTEST_MAIN(WalkMirrorTest)

#include "tst_walkmirror.moc"
