#pragma once

#include <QFileSystemWatcher>
#include <QImage>
#include <QJsonObject>
#include <QPoint>
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
    void setLookAtCopilot(bool enabled);
    void setLookDirection(bool faceLeft);
    void setFreeMovementEnabled(bool enabled);
    bool freeMovementEnabled() const;
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
    void lookAtToggled(bool enabled);
    void freeMovementToggled(bool enabled);
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
    void tick();
    void advanceFreeMovement();
    void scheduleFreeMovement(int delayMs);
    void stopFreeMovement();
    void showContextMenu(const QPoint &globalPosition);
    void loadSheet();
    void watchConfiguration();
    void useVectorFallback();
    void drawFallbackPet(QPainter &painter, const QRect &bounds);

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
    QPoint movementDirection_;
    QString movementRestoreAnimation_;
    QPoint dragOffset_;
    QPoint pressPosition_;
    int frame_ = 0;
    int frameWidth_ = 128;
    int frameHeight_ = 128;
    int framesPerSecond_ = 8;
    int movementRemaining_ = 0;
    qreal scale_ = 1.0;
    bool dragging_ = false;
    bool lookAtCopilot_ = false;
    bool faceLeft_ = false;
    bool freeMovementEnabled_ = false;
    bool moving_ = false;
    bool movementAnimationActive_ = false;
    bool settingMovementAnimation_ = false;
};
