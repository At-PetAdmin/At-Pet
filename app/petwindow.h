#pragma once

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QImage>
#include <QJsonObject>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include <QVector>

class QContextMenuEvent;
class QHideEvent;
class QMouseEvent;
class QPainter;
class QShowEvent;

class PetWindow final : public QWidget {
    Q_OBJECT

public:
    explicit PetWindow(const QString &petDirectory = {}, QWidget *parent = nullptr);

    static QJsonObject withInferredAnimations(const QJsonObject &definition);
    static QString builtinPetDirectory();
    QString petId() const;
    QString petName() const;
    QString animation() const;
    QString speech() const;
    QString petDirectory() const;
    void setAnimation(const QString &name);
    void setAnimationFps(const QString &name, int fps);
    void setSpeech(const QString &text);
    QString randomQuote() const;
    QStringList animationGroup(const QString &groupName) const;
    void setLookAtMouse(bool enabled);
    bool lookAtMouse() const;
    void setLookTarget(const QPoint &globalPosition);
    void setDoNotDisturb(bool enabled);
    bool doNotDisturb() const;
    bool attachedToWindow() const;
    void releaseFromWindow();
    void slideDownBorder();
    void setFreeMovementEnabled(bool enabled);
    bool freeMovementEnabled() const;
    void setGravityEnabled(bool enabled);
    bool gravityEnabled() const;
    void setGravityScale(qreal scale);
    qreal gravityScale() const;
    void reloadDefinition();

signals:
    void animationChanged(const QString &name);
    void petClicked();
    void chatRequested();
    void switchRequested(const QString &petId);
    void importRequested();
    void importFolderRequested();
    void summonAllRequested();
    void deleteRequested(const QString &petId);
    void lookAtMouseToggled(bool enabled);
    void doNotDisturbToggled(bool enabled);
    void attachmentChanged(bool attached);
    void freeMovementToggled(bool enabled);
    void gravityToggled(bool enabled);
    void previewRequested(const QString &petId);
    void settingsRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    enum class AttachmentEdge { None, Top, Left, Right };

    void tick();
    void applyDisplaySize();
    void paintPet(QPainter &painter, const QSize &canvasSize);
    void paintCompact(QPainter &painter);
    void drawSpeechBubble(QPainter &painter, const QRect &bubble, const QString &text,
                          qreal fontPointSize = 9.0);
    QString compactSpeech() const;
    bool tryAttachToWindow(const QPoint &dropPoint, const QPointF &velocity);
    void detachFromWindow();
    void trackAttachedWindow();
    void positionAttachedWindow();
    void startSliding(bool announce = true);
    void advanceSliding();
    void dockToNearestEdge();
    void advanceEdgeWalk();
    void advanceFreeMovement();
    void scheduleFreeMovement(int delayMs);
    void stopFreeMovement();
    void updateMovementAnimation();
    QString movementAnimationForDirection(const QPoint &direction) const;
    void startGravity(const QPointF &initialVelocity = QPointF());
    void stopGravity();
    void advanceGravity();
    QPointF dragReleaseVelocity(const QPoint &releasePosition) const;
    QPoint boundedPosition(const QPoint &position, const QPoint &referencePoint) const;
    void showContextMenu(const QPoint &globalPosition);
    void scheduleRest();
    void beginRest();
    void endRest();
    void cancelRest();
    void resumeMovementAfterRest();
    void loadSheet();
    void watchConfiguration();
    void useVectorFallback();
    void drawFallbackPet(QPainter &painter, const QRect &bounds);

    struct DragSample {
        qint64 timeMs = 0;
        QPoint position;
    };

    QString directory_;
    QString name_ = QStringLiteral("At-Pet");
    QString animation_ = QStringLiteral("idle");
    QString speech_;
    QString spriteSheetPath_;
    QJsonObject definition_;
    QImage spriteSheet_;
    QFileSystemWatcher watcher_;
    QTimer animationTimer_;
    QTimer speechTimer_;
    QTimer movementTimer_;
    QTimer gravityTimer_;
    QTimer attachTimer_;
    QTimer slideTimer_;
    QTimer restTimer_;
    QString restAnimation_;
    QPoint movementDirection_;
    QString movementRestoreAnimation_;
    QString movementAnimationName_;
    QVector<DragSample> dragSamples_;
    QElapsedTimer dragVelocityTimer_;
    QPoint dragOffset_;
    QPoint pressPosition_;
    QPointF lookOffset_;
    QSize baseSize_ = QSize(176, 176);
    QRect doNotDisturbRestoreGeometry_;
    QRect hostBounds_;
    quintptr hostWindow_ = 0;
    AttachmentEdge attachment_ = AttachmentEdge::None;
    int attachmentOffset_ = 0;
    int frame_ = 0;
    int frameWidth_ = 128;
    int frameHeight_ = 128;
    int framesPerSecond_ = 8;
    int movementRemaining_ = 0;
    qreal gravityVelocity_ = 0.0;
    qreal gravityHorizontalVelocity_ = 0.0;
    qreal gravityScale_ = 1.0;
    qreal scale_ = 1.0;
    qreal slideVelocity_ = 0.0;
    bool dragging_ = false;
    bool lookAtMouse_ = false;
    bool faceLeft_ = false;
    bool doNotDisturb_ = false;
    bool sliding_ = false;
    bool freeMovementEnabled_ = false;
    bool gravityEnabled_ = false;
    bool gravityActive_ = false;
    bool movementFacingLeft_ = false;
    bool moving_ = false;
    bool movementAnimationActive_ = false;
    bool settingMovementAnimation_ = false;
    bool resting_ = false;
    bool settingRestAnimation_ = false;
    bool restPausedMovement_ = false;
};
