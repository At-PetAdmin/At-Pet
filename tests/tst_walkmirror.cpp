// Regression test for directional walk mirroring.
//
// PetWindow used to mirror every sprite while walking left, even when the pet configured a
// dedicated walkLeft animation. Left-facing art was therefore flipped back to face right.
// The contract now is:
//   1. A shared "walk" fallback is mirrored when the pet moves left, so it must face right.
//   2. walkLeft/walkRight art is drawn as authored, never mirrored.
//   3. The mouse-gaze flip only applies while no movement animation plays.
//   4. While moving, the walking direction beats the mouse-gaze flip.

#include "petwindow.h"

#include <QTest>

class WalkMirrorTest final : public QObject {
    Q_OBJECT

private slots:
    void sharedWalkMirrorsWhenMovingLeft();
    void directionalArtIsNotMirrored();
    void gazeMirrorsOnlyWhenIdle();
    void movementDirectionBeatsGaze();
};

void WalkMirrorTest::sharedWalkMirrorsWhenMovingLeft() {
    QVERIFY(PetWindow::spriteShouldMirror(true, true, false, false, false));
    QVERIFY(!PetWindow::spriteShouldMirror(true, false, false, false, false));
}

void WalkMirrorTest::directionalArtIsNotMirrored() {
    QVERIFY(!PetWindow::spriteShouldMirror(true, true, true, false, false));
    QVERIFY(!PetWindow::spriteShouldMirror(true, true, true, true, true));
}

void WalkMirrorTest::gazeMirrorsOnlyWhenIdle() {
    QVERIFY(PetWindow::spriteShouldMirror(false, false, false, true, true));
    QVERIFY(!PetWindow::spriteShouldMirror(false, false, false, true, false));
    QVERIFY(!PetWindow::spriteShouldMirror(false, false, false, false, true));
}

void WalkMirrorTest::movementDirectionBeatsGaze() {
    // Gaze points left, the pet walks right: the walk art stays as authored.
    QVERIFY(!PetWindow::spriteShouldMirror(true, false, false, true, true));
    // Gaze points right, the pet walks left: the shared walk still mirrors.
    QVERIFY(PetWindow::spriteShouldMirror(true, true, false, true, false));
}

QTEST_MAIN(WalkMirrorTest)

#include "tst_walkmirror.moc"
