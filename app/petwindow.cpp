#include "petwindow.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QScreen>
#include <QSettings>
#include <QShowEvent>
#include <QHideEvent>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QtMath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
const QStringList builtinAnimations = {
    QStringLiteral("idle"), QStringLiteral("walk"), QStringLiteral("sleep"),
    QStringLiteral("thinking"), QStringLiteral("work"), QStringLiteral("review"),
    QStringLiteral("error"), QStringLiteral("done"), QStringLiteral("cancelled"),
    QStringLiteral("tool"), QStringLiteral("interact"), QStringLiteral("permission")
};

constexpr qreal gravityAcceleration = 0.65;
constexpr qreal gravityTerminalVelocity = 18.0;
constexpr qreal gravityHorizontalDecay = 0.965;
constexpr qreal wallBounceRestitution = 0.6;
constexpr qreal minBounceVelocity = 1.5;
constexpr qreal minGravityScale = 0.1;
constexpr qreal maxGravityScale = 3.0;
constexpr qreal maxFlingVelocity = 30.0;
constexpr qreal minFlingVelocity = 1.5;
constexpr qint64 flingSampleWindowMs = 90;

constexpr qreal compactWindowScale = 0.55;
constexpr int compactMinWidth = 96;
constexpr int compactMinHeight = 104;
constexpr int attachPollMs = 80;
constexpr int slideIntervalMs = 16;
constexpr qreal slideAcceleration = 0.45;
constexpr qreal slideMaxSpeed = 9.0;
constexpr int slideBottomMargin = 8;
constexpr int edgeSnapDepth = 26;
constexpr int titleBarSnapDepth = 34;
constexpr int titleBarOverlap = 8;
constexpr int sideEdgeOverlap = 12;
constexpr int restIdleDelayMinMs = 8000;
constexpr int restIdleDelayMaxMs = 20000;
constexpr int restHoldMinMs = 3500;
constexpr int restHoldMaxMs = 9000;
}

#ifdef Q_OS_WIN
namespace {
struct HostWindowInfo {
    HWND handle = nullptr;
    QRect bounds;
};

bool isShellSurfaceClass(const QString &className) {
    static const QStringList ignored = {
        QStringLiteral("Progman"), QStringLiteral("WorkerW"),
        QStringLiteral("Shell_TrayWnd"), QStringLiteral("Shell_SecondaryTrayWnd"),
        QStringLiteral("Windows.UI.Core.CoreWindow"),
        QStringLiteral("XamlExplorerHostIslandWindow")
    };
    return ignored.contains(className);
}

bool isEligibleHostWindow(HWND window) {
    if (!window || !IsWindowVisible(window) || IsIconic(window))
        return false;
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == GetCurrentProcessId())
        return false;
    if (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
        return false;
    if ((GetWindowLongPtrW(window, GWL_STYLE) & (WS_CAPTION | WS_THICKFRAME)) == 0)
        return false;
    RECT rect{};
    if (!GetWindowRect(window, &rect))
        return false;
    if (rect.right - rect.left < 200 || rect.bottom - rect.top < 120)
        return false;
    wchar_t className[128]{};
    GetClassNameW(window, className, 128);
    return !isShellSurfaceClass(QString::fromWCharArray(className));
}

struct HostWindowSearch {
    QPoint point;
    HostWindowInfo result;
};

BOOL CALLBACK findHostWindow(HWND window, LPARAM data) {
    auto *search = reinterpret_cast<HostWindowSearch *>(data);
    if (!isEligibleHostWindow(window))
        return TRUE;
    RECT rect{};
    if (!GetWindowRect(window, &rect))
        return TRUE;
    const QRect bounds(QPoint(rect.left, rect.top),
                       QSize(rect.right - rect.left, rect.bottom - rect.top));
    const int titleBottom = bounds.top() + qMax(titleBarSnapDepth, 30);
    const bool inTitleBand = search->point.y() >= bounds.top() - titleBarSnapDepth &&
                             search->point.y() <= titleBottom &&
                             search->point.x() >= bounds.left() &&
                             search->point.x() <= bounds.right();
    const bool onVerticalEdge = search->point.y() >= bounds.top() &&
                                search->point.y() <= bounds.bottom() &&
                                (qAbs(search->point.x() - bounds.left()) <= edgeSnapDepth ||
                                 qAbs(search->point.x() - bounds.right()) <= edgeSnapDepth);
    if (!inTitleBand && !onVerticalEdge)
        return TRUE;
    search->result.handle = window;
    search->result.bounds = bounds;
    return FALSE;
}

HostWindowInfo hostWindowAt(const QPoint &point) {
    HostWindowSearch search;
    search.point = point;
    EnumWindows(findHostWindow, reinterpret_cast<LPARAM>(&search));
    return search.result;
}

QRect windowBounds(HWND window) {
    RECT rect{};
    if (!window || !GetWindowRect(window, &rect))
        return {};
    return QRect(QPoint(rect.left, rect.top), QSize(rect.right - rect.left, rect.bottom - rect.top));
}
}
#endif

PetWindow::PetWindow(const QString &petDirectory, QWidget *parent)
    : QWidget(parent), directory_(petDirectory) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);
    resize(176, 176);

    animationTimer_.setTimerType(Qt::PreciseTimer);
    movementTimer_.setSingleShot(true);
    movementTimer_.setTimerType(Qt::PreciseTimer);
    gravityTimer_.setTimerType(Qt::PreciseTimer);
    connect(&animationTimer_, &QTimer::timeout, this, &PetWindow::tick);
    connect(&movementTimer_, &QTimer::timeout, this, &PetWindow::advanceFreeMovement);
    connect(&gravityTimer_, &QTimer::timeout, this, &PetWindow::advanceGravity);
    attachTimer_.setInterval(attachPollMs);
    slideTimer_.setInterval(slideIntervalMs);
    slideTimer_.setTimerType(Qt::PreciseTimer);
    restTimer_.setSingleShot(true);
    connect(&attachTimer_, &QTimer::timeout, this, &PetWindow::trackAttachedWindow);
    connect(&slideTimer_, &QTimer::timeout, this, &PetWindow::advanceSliding);
    connect(&restTimer_, &QTimer::timeout, this, [this] {
        if (resting_) endRest();
        else beginRest();
    });
    connect(&speechTimer_, &QTimer::timeout, this, [this] {
        speech_.clear();
        update();
    });
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this](const QString &) {
        reloadDefinition();
    });
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this](const QString &) {
        reloadDefinition();
    });

    reloadDefinition();
    animationTimer_.start(qMax(30, 1000 / framesPerSecond_));
}

QJsonObject PetWindow::withInferredAnimations(const QJsonObject &definition) {
    if (!definition.value("animations").toObject().isEmpty())
        return definition;

    const QJsonArray rowCounts = definition.value("row_counts").toArray();
    static const QStringList animationNames = {
        QStringLiteral("idle"), QStringLiteral("walk"), QStringLiteral("sleep"),
        QStringLiteral("thinking"), QStringLiteral("work"), QStringLiteral("review"),
        QStringLiteral("error"), QStringLiteral("done"), QStringLiteral("cancelled"),
        QStringLiteral("tool"), QStringLiteral("interact"), QStringLiteral("permission")
    };
    if (rowCounts.isEmpty())
        return definition;

    QJsonObject animations;
    const int rowCount = qMin(rowCounts.size(), animationNames.size());
    for (int row = 0; row < rowCount; ++row) {
        const int frames = rowCounts.at(row).toInt();
        if (frames > 0)
            animations.insert(animationNames.at(row), QJsonObject{{"row", row}, {"frames", frames}});
    }
    if (animations.isEmpty())
        return definition;

    QJsonObject normalized = definition;
    normalized.insert("animations", animations);
    return normalized;
}

QString PetWindow::petId() const {
    return directory_.isEmpty() ? QStringLiteral("default") : QFileInfo(directory_).fileName();
}

QString PetWindow::petName() const { return name_; }
QString PetWindow::animation() const { return animation_; }
QString PetWindow::speech() const { return speech_; }
QString PetWindow::petDirectory() const { return directory_; }

namespace {
QString animationFpsKey(const QString &petId, const QString &animation) {
    return QStringLiteral("animationFps/%1/%2")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(petId)),
             QString::fromLatin1(QUrl::toPercentEncoding(animation)));
}
}

void PetWindow::setAnimationFps(const QString &name, int fps) {
    if (name == animation_)
        animationTimer_.setInterval(qMax(30, 1000 / qBound(1, fps, 30)));
}

void PetWindow::setAnimation(const QString &name) {
    if (!builtinAnimations.contains(name) && !definition_.value("animations").toObject().contains(name))
        return;

    const bool externalAnimation = !settingMovementAnimation_;
    const bool cancelledMovement = externalAnimation && (moving_ || movementAnimationActive_);
    if (cancelledMovement) {
        movementTimer_.stop();
        moving_ = false;
        movementRemaining_ = 0;
        movementDirection_ = {};
        movementAnimationActive_ = false;
        movementAnimationName_.clear();
        movementRestoreAnimation_.clear();
    }
    if (animation_ != name) {
        animation_ = name;
        frame_ = 0;
        const QJsonObject animation = definition_.value("animations").toObject().value(name).toObject();
        const int defaultFps = qBound(1, animation.value("fps").toInt(framesPerSecond_), 30);
        const int fps = QSettings().value(animationFpsKey(petId(), name), defaultFps).toInt();
        animationTimer_.setInterval(qMax(30, 1000 / qBound(1, fps, 30)));
        emit animationChanged(animation_);
        update();
    }
    if (cancelledMovement)
        scheduleFreeMovement(QRandomGenerator::global()->bounded(1200, 3201));
    if (!settingRestAnimation_ && !settingMovementAnimation_) {
        if (name == QStringLiteral("idle")) {
            if (!movementAnimationActive_ && !moving_)
                scheduleRest();
        } else {
            cancelRest();
        }
    }
}

void PetWindow::setSpeech(const QString &text) {
    speech_ = text.left(180);
    speechTimer_.start(5000);
    update();
}

void PetWindow::setLookAtMouse(bool enabled) {
    if (lookAtMouse_ == enabled)
        return;
    lookAtMouse_ = enabled;
    if (!enabled) {
        lookOffset_ = {};
        faceLeft_ = false;
    }
    update();
}

bool PetWindow::lookAtMouse() const {
    return lookAtMouse_;
}

void PetWindow::setLookTarget(const QPoint &globalPosition) {
    if (!lookAtMouse_)
        return;
    const QPoint delta = globalPosition - frameGeometry().center();
    const QPointF offset(qBound(-1.0, delta.x() / static_cast<double>(qMax(80, width())), 1.0),
                         qBound(-1.0, delta.y() / static_cast<double>(qMax(80, height())), 1.0));
    if (qAbs(offset.x() - lookOffset_.x()) < 0.01 && qAbs(offset.y() - lookOffset_.y()) < 0.01)
        return;
    lookOffset_ = offset;
    faceLeft_ = offset.x() < -0.08;
    update();
}

QString PetWindow::randomQuote() const {
    QStringList quotes;
    const auto collect = [&quotes](const QJsonValue &value) {
        if (value.isString()) {
            const QString text = value.toString().trimmed();
            if (!text.isEmpty())
                quotes.append(text);
            return;
        }
        if (!value.isArray())
            return;
        for (const QJsonValue &entry : value.toArray()) {
            QString text;
            if (entry.isString())
                text = entry.toString();
            else if (entry.isObject())
                text = entry.toObject().value("text").toString();
            text = text.trimmed();
            if (!text.isEmpty())
                quotes.append(text);
        }
    };
    collect(definition_.value("quotes"));
    collect(definition_.value("pokeQuotes"));
    if (quotes.isEmpty())
        return {};
    return quotes.at(QRandomGenerator::global()->bounded(int(quotes.size())));
}

QStringList PetWindow::animationGroup(const QString &groupName) const {
    const auto names = [](const QJsonValue &value) {
        QStringList result;
        const auto append = [&result](QString name) {
            name = name.trimmed();
            if (!name.isEmpty() && !result.contains(name))
                result.append(name);
        };
        if (value.isString()) {
            for (const QString &part : value.toString().split(QLatin1Char(',')))
                append(part);
        } else if (value.isArray()) {
            for (const QJsonValue &entry : value.toArray()) {
                if (entry.isString())
                    append(entry.toString());
                else if (entry.isObject())
                    append(entry.toObject().value("name").toString());
            }
        }
        return result;
    };

    QStringList requested = names(definition_.value("animationGroups").toObject().value(groupName));
    if (requested.isEmpty())
        requested = names(definition_.value(groupName));
    if (requested.isEmpty() && groupName == QStringLiteral("rest"))
        requested = {QStringLiteral("idle"), QStringLiteral("sleep")};

    const QJsonObject animations = definition_.value("animations").toObject();
    QStringList available;
    for (const QString &name : requested) {
        if (animations.contains(name))
            available.append(name);
    }
    return available;
}

void PetWindow::scheduleRest() {
    restTimer_.stop();
    if (doNotDisturb_ || isHidden())
        return;
    if (animationGroup(QStringLiteral("rest")).isEmpty())
        return;
    restTimer_.start(QRandomGenerator::global()->bounded(restIdleDelayMinMs, restIdleDelayMaxMs));
}

void PetWindow::beginRest() {
    const QStringList group = animationGroup(QStringLiteral("rest"));
    if (group.isEmpty() || doNotDisturb_ || isHidden() || dragging_ || sliding_ || gravityActive_ ||
        attachedToWindow()) {
        scheduleRest();
        return;
    }
    if (animation_ != QStringLiteral("idle") || moving_ || movementAnimationActive_) {
        restTimer_.start(qMax(600, restIdleDelayMinMs / 4));
        return;
    }
    if (freeMovementEnabled_ && movementTimer_.isActive()) {
        stopFreeMovement();
        restPausedMovement_ = true;
    }
    QStringList candidates = group;
    if (candidates.size() > 1) {
        QStringList preferred = candidates;
        preferred.removeAll(animation_);
        if (preferred.isEmpty())
            preferred = group;
        if (preferred.size() > 1) {
            QStringList nonRepeat = preferred;
            nonRepeat.removeAll(restAnimation_);
            if (!nonRepeat.isEmpty())
                preferred = nonRepeat;
        }
        candidates = preferred;
    }
    const QString pick = candidates.at(QRandomGenerator::global()->bounded(int(candidates.size())));
    restAnimation_ = pick;
    resting_ = true;
    settingRestAnimation_ = true;
    setAnimation(pick);
    settingRestAnimation_ = false;
    restTimer_.start(QRandomGenerator::global()->bounded(restHoldMinMs, restHoldMaxMs));
}

void PetWindow::endRest() {
    resting_ = false;
    settingRestAnimation_ = true;
    setAnimation(QStringLiteral("idle"));
    settingRestAnimation_ = false;
    scheduleRest();
    resumeMovementAfterRest();
}

void PetWindow::resumeMovementAfterRest() {
    if (!restPausedMovement_)
        return;
    restPausedMovement_ = false;
    if (freeMovementEnabled_ && isVisible() && !doNotDisturb_)
        scheduleFreeMovement(QRandomGenerator::global()->bounded(1200, 3201));
}

void PetWindow::cancelRest() {
    restTimer_.stop();
    const bool restoreIdle = resting_ && !restAnimation_.isEmpty() && animation_ == restAnimation_;
    resting_ = false;
    restAnimation_.clear();
    if (restoreIdle) {
        settingRestAnimation_ = true;
        setAnimation(QStringLiteral("idle"));
        settingRestAnimation_ = false;
    }
    resumeMovementAfterRest();
}

void PetWindow::setDoNotDisturb(bool enabled) {
    if (doNotDisturb_ == enabled)
        return;
    doNotDisturb_ = enabled;
    emit doNotDisturbToggled(enabled);
    if (enabled) {
        doNotDisturbRestoreGeometry_ = frameGeometry();
        detachFromWindow();
        stopFreeMovement();
        stopGravity();
        cancelRest();
        speech_.clear();
        applyDisplaySize();
        dockToNearestEdge();
        setWindowOpacity(0.94);
        setSpeech(tr("Do not disturb — summon me from the tray."));
    } else {
        applyDisplaySize();
        if (!doNotDisturbRestoreGeometry_.isNull()) {
            move(boundedPosition(doNotDisturbRestoreGeometry_.topLeft(),
                                 doNotDisturbRestoreGeometry_.center()));
            doNotDisturbRestoreGeometry_ = {};
        }
        setWindowOpacity(1.0);
        speech_.clear();
        update();
        if (isVisible()) {
            if (gravityEnabled_)
                startGravity();
            else if (freeMovementEnabled_)
                scheduleFreeMovement(QRandomGenerator::global()->bounded(900, 3201));
            if (animation_ == QStringLiteral("idle"))
                scheduleRest();
        }
    }
}

bool PetWindow::doNotDisturb() const {
    return doNotDisturb_;
}

bool PetWindow::attachedToWindow() const {
    return attachment_ != AttachmentEdge::None;
}

void PetWindow::releaseFromWindow() {
    if (!attachedToWindow())
        return;
    detachFromWindow();
    setSpeech(tr("Let go of the window."));
}

void PetWindow::slideDownBorder() {
    startSliding(true);
}

void PetWindow::applyDisplaySize() {
    if (baseSize_.isEmpty())
        baseSize_ = QSize(176, 176);
    if (doNotDisturb_) {
        resize(qMax(compactMinWidth, qRound(baseSize_.width() * compactWindowScale)),
               qMax(compactMinHeight, qRound(baseSize_.height() * compactWindowScale)));
    } else {
        resize(baseSize_);
    }
    update();
}

void PetWindow::dockToNearestEdge() {
    QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    const QRect area = screen->availableGeometry();
    const bool leftSide = frameGeometry().center().x() <= area.center().x();
    const int targetX = leftSide ? area.left() + 6 : area.right() - width() - 6;
    const int targetY = qBound(area.top() + 6, y(),
                               qMax(area.top() + 6, area.bottom() - height() - 6));
    move(targetX, targetY);
}

bool PetWindow::tryAttachToWindow(const QPoint &dropPoint, const QPointF &velocity) {
#ifdef Q_OS_WIN
    const HostWindowInfo host = hostWindowAt(dropPoint);
    if (!host.handle || host.bounds.isEmpty())
        return false;

    const QRect &bounds = host.bounds;
    AttachmentEdge edge = AttachmentEdge::Top;
    if (dropPoint.y() > bounds.top() + qMax(titleBarSnapDepth, 30)) {
        const int leftDistance = qAbs(dropPoint.x() - bounds.left());
        const int rightDistance = qAbs(dropPoint.x() - bounds.right());
        if (qMin(leftDistance, rightDistance) > edgeSnapDepth)
            return false;
        edge = leftDistance <= rightDistance ? AttachmentEdge::Left : AttachmentEdge::Right;
    }

    hostWindow_ = quintptr(host.handle);
    hostBounds_ = bounds;
    attachment_ = edge;
    sliding_ = false;
    slideTimer_.stop();
    stopFreeMovement();
    stopGravity();
    if (edge == AttachmentEdge::Top) {
        attachmentOffset_ = qBound(0, x() - bounds.left(), qMax(0, bounds.width() - width()));
    } else {
        attachmentOffset_ = qBound(0, y() - bounds.top(),
                                   qMax(0, bounds.height() - height() + slideBottomMargin));
    }
    positionAttachedWindow();
    attachTimer_.start(attachPollMs);
    setSpeech(tr("Perched on the window edge."));
    emit attachmentChanged(true);
    if (edge == AttachmentEdge::Top && freeMovementEnabled_)
        scheduleFreeMovement(QRandomGenerator::global()->bounded(700, 1601));
    else if (edge != AttachmentEdge::Top && velocity.y() > 5.0)
        startSliding(false);
    else if (edge != AttachmentEdge::Top)
        setSpeech(tr("Hanging on the window frame…"));
    update();
    return true;
#else
    Q_UNUSED(dropPoint);
    Q_UNUSED(velocity);
    return false;
#endif
}

void PetWindow::detachFromWindow() {
    if (attachment_ == AttachmentEdge::None)
        return;
    attachTimer_.stop();
    slideTimer_.stop();
    sliding_ = false;
    slideVelocity_ = 0.0;
    hostWindow_ = 0;
    hostBounds_ = {};
    attachment_ = AttachmentEdge::None;
    attachmentOffset_ = 0;
    stopFreeMovement();
    emit attachmentChanged(false);
    update();
    if (isVisible() && !doNotDisturb_) {
        if (gravityEnabled_)
            startGravity();
        else if (freeMovementEnabled_)
            scheduleFreeMovement(QRandomGenerator::global()->bounded(900, 3201));
    }
}

void PetWindow::trackAttachedWindow() {
#ifdef Q_OS_WIN
    const HWND window = reinterpret_cast<HWND>(hostWindow_);
    if (!window || !IsWindow(window) || !IsWindowVisible(window) || IsIconic(window)) {
        detachFromWindow();
        return;
    }
    const QRect bounds = windowBounds(window);
    if (bounds.isEmpty()) {
        detachFromWindow();
        return;
    }
    if (bounds != hostBounds_) {
        hostBounds_ = bounds;
        positionAttachedWindow();
    }
#else
    detachFromWindow();
#endif
}

void PetWindow::positionAttachedWindow() {
    if (attachment_ == AttachmentEdge::None || hostBounds_.isEmpty())
        return;
    QScreen *screen = QGuiApplication::screenAt(hostBounds_.center());
    if (!screen)
        screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    const QRect area = screen->availableGeometry();
    QPoint target = pos();
    switch (attachment_) {
    case AttachmentEdge::Top:
        attachmentOffset_ = qBound(0, attachmentOffset_, qMax(0, hostBounds_.width() - width()));
        target = {hostBounds_.left() + attachmentOffset_,
                  hostBounds_.top() - height() + titleBarOverlap};
        break;
    case AttachmentEdge::Left:
    case AttachmentEdge::Right:
        attachmentOffset_ = qBound(0, attachmentOffset_,
                                   qMax(0, hostBounds_.height() - height() + slideBottomMargin));
        target = {attachment_ == AttachmentEdge::Left
                      ? hostBounds_.left() - width() + sideEdgeOverlap
                      : hostBounds_.right() - sideEdgeOverlap,
                  hostBounds_.top() + attachmentOffset_};
        break;
    case AttachmentEdge::None:
        return;
    }
    target.setX(qBound(area.left(), target.x(), qMax(area.left(), area.right() - width() + 1)));
    target.setY(qBound(area.top(), target.y(), qMax(area.top(), area.bottom() - height() + 1)));
    if (target != pos())
        move(target);
}

void PetWindow::startSliding(bool announce) {
    if (attachment_ == AttachmentEdge::None)
        return;
    if (attachment_ == AttachmentEdge::Top) {
        const int center = x() + width() / 2;
        const bool useLeft = center - hostBounds_.left() <= hostBounds_.right() - center;
        attachment_ = useLeft ? AttachmentEdge::Left : AttachmentEdge::Right;
        attachmentOffset_ = qMax(0, y() - hostBounds_.top());
        positionAttachedWindow();
    }
    sliding_ = true;
    slideVelocity_ = 2.0;
    slideTimer_.start(slideIntervalMs);
    if (announce)
        setSpeech(tr("Sliding down the window frame…"));
    update();
}

void PetWindow::advanceSliding() {
    if (!sliding_ || attachment_ == AttachmentEdge::None) {
        sliding_ = false;
        slideTimer_.stop();
        return;
    }
    slideVelocity_ = qMin(slideMaxSpeed, slideVelocity_ + slideAcceleration * gravityScale_);
    attachmentOffset_ += qRound(slideVelocity_);
    const int maxOffset = qMax(0, hostBounds_.height() - height() + slideBottomMargin);
    if (attachmentOffset_ >= maxOffset) {
        attachmentOffset_ = maxOffset;
        sliding_ = false;
        slideTimer_.stop();
        positionAttachedWindow();
        setSpeech(tr("Hanging on the window frame."));
        return;
    }
    positionAttachedWindow();
}

void PetWindow::advanceEdgeWalk() {
    if (attachment_ != AttachmentEdge::Top || !freeMovementEnabled_ || !isVisible() ||
        gravityActive_ || dragging_ || doNotDisturb_) {
        stopFreeMovement();
        return;
    }
    if (!moving_) {
        movementRemaining_ = QRandomGenerator::global()->bounded(14, 44);
        movementDirection_ = QPoint(QRandomGenerator::global()->bounded(2) == 0 ? -1 : 1, 0);
        moving_ = true;
        updateMovementAnimation();
    }
    const int maxOffset = qMax(0, hostBounds_.width() - width());
    attachmentOffset_ += movementDirection_.x() * 2;
    if (attachmentOffset_ <= 0) {
        attachmentOffset_ = 0;
        movementDirection_.setX(1);
    } else if (attachmentOffset_ >= maxOffset) {
        attachmentOffset_ = maxOffset;
        movementDirection_.setX(-1);
    }
    movementFacingLeft_ = movementDirection_.x() < 0;
    positionAttachedWindow();
    --movementRemaining_;
    if (movementRemaining_ <= 0) {
        stopFreeMovement();
        scheduleFreeMovement(QRandomGenerator::global()->bounded(1200, 3401));
        return;
    }
    movementTimer_.start(40);
}

void PetWindow::setFreeMovementEnabled(bool enabled) {
    if (freeMovementEnabled_ == enabled)
        return;
    freeMovementEnabled_ = enabled;
    if (!enabled) {
        stopFreeMovement();
    } else if (isVisible()) {
        scheduleFreeMovement(QRandomGenerator::global()->bounded(900, 3201));
    }
}

bool PetWindow::freeMovementEnabled() const {
    return freeMovementEnabled_;
}

void PetWindow::setGravityEnabled(bool enabled) {
    if (gravityEnabled_ == enabled)
        return;
    gravityEnabled_ = enabled;
    if (enabled) {
        startGravity();
    } else {
        stopGravity();
        if (freeMovementEnabled_ && isVisible())
            scheduleFreeMovement(QRandomGenerator::global()->bounded(900, 3201));
    }
}

bool PetWindow::gravityEnabled() const {
    return gravityEnabled_;
}

void PetWindow::setGravityScale(qreal scale) {
    gravityScale_ = qBound(minGravityScale, scale, maxGravityScale);
}

qreal PetWindow::gravityScale() const {
    return gravityScale_;
}

void PetWindow::startGravity(const QPointF &initialVelocity) {
    if (!gravityEnabled_ || !isVisible() || dragging_ ||
        attachment_ != AttachmentEdge::None || doNotDisturb_)
        return;
    stopFreeMovement();

    QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    const QRect area = screen->availableGeometry();
    const int targetY = qMax(area.top(), area.bottom() - height() + 1);
    if (y() >= targetY) {
        move(x(), targetY);
        if (initialVelocity.isNull()) {
            gravityActive_ = false;
            if (freeMovementEnabled_)
                scheduleFreeMovement(QRandomGenerator::global()->bounded(900, 3201));
            return;
        }
    }

    gravityActive_ = true;
    gravityVelocity_ = initialVelocity.y();
    gravityHorizontalVelocity_ = initialVelocity.x();
    gravityTimer_.start(16);
}

void PetWindow::stopGravity() {
    gravityTimer_.stop();
    gravityVelocity_ = 0.0;
    gravityHorizontalVelocity_ = 0.0;
    gravityActive_ = false;
}

void PetWindow::advanceGravity() {
    if (!gravityActive_ || !gravityEnabled_ || dragging_ || !isVisible()) {
        stopGravity();
        return;
    }

    QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen) {
        stopGravity();
        return;
    }

    const QRect area = screen->availableGeometry();
    const int minX = area.left();
    const int maxX = qMax(minX, area.right() - width() + 1);
    const int targetY = qMax(area.top(), area.bottom() - height() + 1);

    if (y() < targetY || gravityVelocity_ < 0.0)
        gravityVelocity_ = qMin<qreal>(gravityVelocity_ + gravityAcceleration * gravityScale_,
                                       gravityTerminalVelocity);
    else
        gravityVelocity_ = 0.0;

    gravityHorizontalVelocity_ *= gravityHorizontalDecay;
    if (qAbs(gravityHorizontalVelocity_) < 0.5)
        gravityHorizontalVelocity_ = 0.0;

    int nextX = x() + qRound(gravityHorizontalVelocity_);
    int nextY = y() + qRound(gravityVelocity_);
    if (nextX < minX || nextX > maxX) {
        nextX = qBound(minX, nextX, maxX);
        gravityHorizontalVelocity_ = qAbs(gravityHorizontalVelocity_) >= minBounceVelocity
            ? -gravityHorizontalVelocity_ * wallBounceRestitution : 0.0;
    }
    if (nextY <= area.top()) {
        nextY = area.top();
        gravityVelocity_ = qAbs(gravityVelocity_) >= minBounceVelocity
            ? -gravityVelocity_ * wallBounceRestitution : 0.0;
    } else if (nextY >= targetY) {
        nextY = targetY;
        gravityVelocity_ = 0.0;
    }
    move(nextX, nextY);

    if (y() >= targetY && gravityVelocity_ == 0.0 && gravityHorizontalVelocity_ == 0.0) {
        stopGravity();
        scheduleFreeMovement(QRandomGenerator::global()->bounded(900, 3201));
    }
}

void PetWindow::scheduleFreeMovement(int delayMs) {
    if (!freeMovementEnabled_ || !isVisible() || dragging_ || gravityActive_ ||
        doNotDisturb_ || movementTimer_.isActive())
        return;
    if (attachment_ != AttachmentEdge::None && attachment_ != AttachmentEdge::Top)
        return;
    movementTimer_.start(qMax(0, delayMs));
}

void PetWindow::stopFreeMovement() {
    movementTimer_.stop();
    const bool restoreAnimation = movementAnimationActive_ &&
                                  animation_ == movementAnimationName_ &&
                                  !movementRestoreAnimation_.isEmpty();
    const QString animationToRestore = movementRestoreAnimation_;
    moving_ = false;
    movementRemaining_ = 0;
    movementDirection_ = {};
    movementAnimationActive_ = false;
    movementAnimationName_.clear();
    movementRestoreAnimation_.clear();
    if (restoreAnimation && animation_ != animationToRestore)
        setAnimation(animationToRestore);
}

QString PetWindow::movementAnimationForDirection(const QPoint &direction) const {
    const QJsonObject animations = definition_.value("animations").toObject();
    if (spriteSheet_.isNull())
        return QStringLiteral("walk");

    if (direction.x() == 0)
        return animations.contains(QStringLiteral("walk")) ? QStringLiteral("walk") : QString();

    const QStringList names = direction.x() < 0
        ? QStringList{QStringLiteral("walkLeft"), QStringLiteral("walk_left"), QStringLiteral("walk-left")}
        : QStringList{QStringLiteral("walkRight"), QStringLiteral("walk_right"), QStringLiteral("walk-right")};
    for (const QString &name : names) {
        if (animations.contains(name))
            return name;
    }
    return animations.contains(QStringLiteral("walk")) ? QStringLiteral("walk") : QString();
}

void PetWindow::updateMovementAnimation() {
    if (!moving_)
        return;
    if (movementDirection_.x() != 0)
        movementFacingLeft_ = movementDirection_.x() < 0;

    const QString desiredAnimation = movementAnimationForDirection(movementDirection_);
    if (desiredAnimation.isEmpty()) {
        if (movementAnimationActive_ && animation_ == movementAnimationName_ &&
            !movementRestoreAnimation_.isEmpty()) {
            settingMovementAnimation_ = true;
            setAnimation(movementRestoreAnimation_);
            settingMovementAnimation_ = false;
        }
        movementAnimationActive_ = false;
        movementAnimationName_.clear();
        movementRestoreAnimation_.clear();
        return;
    }

    if (!movementAnimationActive_)
        movementRestoreAnimation_ = animation_;
    movementAnimationActive_ = true;
    movementAnimationName_ = desiredAnimation;
    settingMovementAnimation_ = true;
    setAnimation(desiredAnimation);
    settingMovementAnimation_ = false;
}

void PetWindow::advanceFreeMovement() {
    if (attachment_ != AttachmentEdge::None) {
        advanceEdgeWalk();
        return;
    }
    if (!freeMovementEnabled_ || !isVisible() || gravityActive_) {
        stopFreeMovement();
        return;
    }
    if (dragging_) {
        stopFreeMovement();
        return;
    }

    QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (gravityEnabled_ && screen) {
        const QRect area = screen->availableGeometry();
        const int floorY = qMax(area.top(), area.bottom() - height() + 1);
        if (y() < floorY) {
            startGravity();
            return;
        }
    }
    if (!screen) {
        stopFreeMovement();
        scheduleFreeMovement(QRandomGenerator::global()->bounded(1800, 4501));
        return;
    }

    if (!moving_) {
        if (animation_ != QStringLiteral("idle")) {
            scheduleFreeMovement(QRandomGenerator::global()->bounded(1800, 4501));
            return;
        }
        movementRemaining_ = QRandomGenerator::global()->bounded(20, 56);
        QVector<QPoint> directions = {QPoint(-1, 0), QPoint(1, 0)};
        if (!gravityEnabled_)
            directions << QPoint(0, -1) << QPoint(0, 1);
        movementDirection_ = directions.at(QRandomGenerator::global()->bounded(int(directions.size())));
        moving_ = true;
        updateMovementAnimation();
    }

    const QRect area = screen->availableGeometry();
    const int minX = area.left();
    const int minY = area.top();
    const int maxX = qMax(minX, area.right() - width() + 1);
    const int maxY = qMax(minY, area.bottom() - height() + 1);
    QVector<QPoint> availableDirections;
    if (x() > minX) availableDirections.append(QPoint(-1, 0));
    if (x() < maxX) availableDirections.append(QPoint(1, 0));
    if (!gravityEnabled_) {
        if (y() > minY) availableDirections.append(QPoint(0, -1));
        if (y() < maxY) availableDirections.append(QPoint(0, 1));
    }
    if (availableDirections.isEmpty()) {
        stopFreeMovement();
        scheduleFreeMovement(QRandomGenerator::global()->bounded(1800, 4501));
        return;
    }
    if (!availableDirections.contains(movementDirection_)) {
        movementDirection_ = availableDirections.at(QRandomGenerator::global()->bounded(availableDirections.size()));
        updateMovementAnimation();
    }

    const QPoint nextPosition = pos() + movementDirection_ * 3;
    const QPoint clampedPosition(qBound(minX, nextPosition.x(), maxX),
                                 qBound(minY, nextPosition.y(), maxY));
    move(clampedPosition);
    --movementRemaining_;
    if (movementRemaining_ <= 0) {
        stopFreeMovement();
        scheduleFreeMovement(QRandomGenerator::global()->bounded(1500, 4201));
        return;
    }
    movementTimer_.start(40);
}

void PetWindow::watchConfiguration() {
    const QString configPath = directory_.isEmpty() ? QString() : QDir(directory_).filePath("pet.json");
    for (const QString &path : watcher_.files())
        watcher_.removePath(path);
    for (const QString &path : watcher_.directories())
        watcher_.removePath(path);
    if (directory_.isEmpty())
        return;
    if (!configPath.isEmpty() && QFileInfo::exists(configPath))
        watcher_.addPath(configPath);
    if (!spriteSheetPath_.isEmpty() && QFileInfo::exists(spriteSheetPath_))
        watcher_.addPath(spriteSheetPath_);
    if (!directory_.isEmpty() && QFileInfo::exists(directory_))
        watcher_.addPath(directory_);
}

QString PetWindow::builtinPetDirectory() {
    return QStringLiteral(":/pets/default");
}

void PetWindow::useVectorFallback() {
    definition_ = {};
    name_ = QStringLiteral("At-Pet");
    spriteSheetPath_.clear();
    spriteSheet_ = {};
    animation_ = QStringLiteral("idle");
    frame_ = 0;
    baseSize_ = QSize(176, 176);
    applyDisplaySize();
    watchConfiguration();
    update();
}

void PetWindow::reloadDefinition() {
    const QString sourceDirectory = directory_.isEmpty() ? builtinPetDirectory() : directory_;

    QFile config(QDir(sourceDirectory).filePath("pet.json"));
    if (!config.open(QIODevice::ReadOnly)) {
        if (directory_.isEmpty()) {
            useVectorFallback();
            return;
        }
        setSpeech(tr("pet.json not found"));
        return;
    }
    QByteArray configBytes = config.readAll();
    if (configBytes.startsWith("\xEF\xBB\xBF"))
        configBytes.remove(0, 3);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(configBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (directory_.isEmpty()) {
            useVectorFallback();
            return;
        }
        setSpeech(tr("Invalid pet.json"));
        watchConfiguration();
        return;
    }

    definition_ = withInferredAnimations(document.object());
    name_ = definition_.value("name").toString(QFileInfo(directory_).fileName());
    const QJsonValue cell = definition_.value("cell");
    int configuredWidth = definition_.value("frameWidth").toInt();
    int configuredHeight = definition_.value("frameHeight").toInt();
    if (cell.isArray() && cell.toArray().size() >= 2) {
        if (configuredWidth <= 0) configuredWidth = cell.toArray().at(0).toInt(128);
        if (configuredHeight <= 0) configuredHeight = cell.toArray().at(1).toInt(128);
    } else if (cell.isObject()) {
        const QJsonObject cellObject = cell.toObject();
        if (configuredWidth <= 0) configuredWidth = cellObject.value("width").toInt(128);
        if (configuredHeight <= 0) configuredHeight = cellObject.value("height").toInt(128);
    }
    frameWidth_ = qBound(1, configuredWidth > 0 ? configuredWidth : 128, 4096);
    frameHeight_ = qBound(1, configuredHeight > 0 ? configuredHeight : 128, 4096);
    framesPerSecond_ = qBound(1, definition_.value("fps").toInt(8), 30);
    const double defaultScale = cell.isUndefined() ? 4.0 : 1.0;
    scale_ = qBound(0.25, definition_.value("scale").toDouble(defaultScale), 8.0);
    QString sheetName = definition_.value("sprite").toString();
    if (sheetName.isEmpty()) sheetName = definition_.value("spriteSheet").toString();
    if (!sheetName.isEmpty() && QFileInfo(sheetName).fileName() != sheetName) {
        setSpeech(tr("Sprite path must be a file name"));
        sheetName.clear();
    }
    const QString nextPath = sheetName.isEmpty() ? QString() : QDir(sourceDirectory).filePath(sheetName);
    if (spriteSheetPath_ != nextPath) {
        spriteSheetPath_ = nextPath;
        loadSheet();
    } else if (!spriteSheetPath_.isEmpty()) {
        loadSheet();
    }
    const int displayWidth = qBound(80, qRound(frameWidth_ * scale_), 480);
    const int displayHeight = qBound(80, qRound(frameHeight_ * scale_), 480);
    baseSize_ = QSize(displayWidth + 16, displayHeight + 56);
    applyDisplaySize();
    if (attachment_ != AttachmentEdge::None)
        positionAttachedWindow();
    const QString defaultAnimation = definition_.value("defaultAnimation").toString();
    if (!defaultAnimation.isEmpty() && definition_.value("animations").toObject().contains(defaultAnimation))
        animation_ = defaultAnimation;
    const QJsonObject currentAnimation = definition_.value("animations").toObject().value(animation_).toObject();
    const int defaultFps = qBound(1, currentAnimation.value("fps").toInt(framesPerSecond_), 30);
    const int fps = QSettings().value(animationFpsKey(petId(), animation_), defaultFps).toInt();
    animationTimer_.setInterval(qMax(30, 1000 / qBound(1, fps, 30)));
    watchConfiguration();
    cancelRest();
    if (animation_ == QStringLiteral("idle"))
        scheduleRest();
    update();
}

void PetWindow::loadSheet() {
    spriteSheet_ = QImage(spriteSheetPath_);
}

void PetWindow::tick() {
    ++frame_;
    update();
}

void PetWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (attachment_ != AttachmentEdge::None)
        attachTimer_.start(attachPollMs);
    if (doNotDisturb_)
        return;
    scheduleRest();
    if (gravityEnabled_)
        startGravity();
    else if (freeMovementEnabled_)
        scheduleFreeMovement(QRandomGenerator::global()->bounded(900, 3201));
}

void PetWindow::hideEvent(QHideEvent *event) {
    stopFreeMovement();
    stopGravity();
    cancelRest();
    attachTimer_.stop();
    slideTimer_.stop();
    sliding_ = false;
    QWidget::hideEvent(event);
}

void PetWindow::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (doNotDisturb_) {
        paintCompact(painter);
        return;
    }
    paintPet(painter, size());
    if (!speech_.isEmpty())
        drawSpeechBubble(painter, QRect(2, 1, width() - 4, 43), speech_);
}

void PetWindow::paintCompact(QPainter &painter) {
    const int bubbleHeight = qBound(30, height() / 2, 42);
    const bool hasSpeech = !speech_.isEmpty();
    const QRect bubble(1, 1, width() - 2, bubbleHeight);
    const QRect petArea(2, hasSpeech ? bubble.bottom() + 2 : 2, width() - 4,
                        height() - (hasSpeech ? bubble.bottom() + 4 : 4));

    const QSize canvas = baseSize_.isEmpty() ? QSize(176, 176) : baseSize_;
    const int spriteHeight = spriteSheet_.isNull()
        ? canvas.height() - 24
        : qMin(canvas.height() - 56, qRound(frameHeight_ * scale_));
    const int spriteTop = spriteSheet_.isNull()
        ? 20
        : canvas.height() - spriteHeight - 8;

    painter.save();
    painter.setClipRect(petArea);
    const qreal factor = qMin(qreal(petArea.width()) / qMax(1, canvas.width()),
                              qreal(petArea.height()) / qMax(1, spriteHeight));
    painter.translate(petArea.left(), petArea.top());
    painter.scale(factor, factor);
    painter.translate(0, -spriteTop);
    paintPet(painter, canvas);
    painter.restore();

    if (hasSpeech)
        drawSpeechBubble(painter, bubble, compactSpeech(), 7.0);
}

QString PetWindow::compactSpeech() const {
    constexpr int limit = 26;
    QString text = speech_.simplified();
    if (text.size() > limit)
        text = text.left(limit - 1) + QChar(0x2026);
    return text;
}

void PetWindow::drawSpeechBubble(QPainter &painter, const QRect &bubble, const QString &text,
                                 qreal fontPointSize) {
    painter.setPen(QPen(QColor(75, 72, 82), 1.2));
    painter.setBrush(QColor(255, 255, 255, 245));
    painter.drawRoundedRect(bubble, 12, 12);
    painter.setPen(QColor(42, 42, 49));
    QFont font(QStringLiteral("Segoe UI"));
    font.setPointSizeF(fontPointSize);
    painter.setFont(font);
    painter.drawText(bubble.adjusted(8, 4, -8, -4), Qt::AlignCenter | Qt::TextWordWrap, text);
}

void PetWindow::paintPet(QPainter &painter, const QSize &canvasSize) {
    const int canvasWidth = canvasSize.width();
    const int canvasHeight = canvasSize.height();
    const int spriteRoom = qMax(1, canvasHeight - 56);
    const QRect bounds = spriteSheet_.isNull()
        ? QRect(8, 20, canvasWidth - 16, canvasHeight - 24)
        : QRect(8, canvasHeight - qMin(spriteRoom, qRound(frameHeight_ * scale_)) - 8,
                qMin(canvasWidth - 16, qRound(frameWidth_ * scale_)),
                qMin(spriteRoom, qRound(frameHeight_ * scale_)));

    const QJsonObject animations = definition_.value("animations").toObject();
    const QJsonObject animation = animations.value(animation_).toObject();
    if (!spriteSheet_.isNull() && !animation.isEmpty()) {
        QVector<int> frames;
        const QJsonValue configuredFrames = animation.value("frames");
        if (configuredFrames.isArray()) {
            for (const QJsonValue &value : configuredFrames.toArray()) {
                if (frames.size() == 256) break;
                frames.append(qMax(0, value.toInt()));
            }
        } else if (animation.contains("start") || animation.contains("end")) {
            const int first = qMax(0, animation.value("start").toInt(0));
            const int last = qMax(first, animation.value("end").toInt(first));
            for (int i = first; i <= last && i - first < 256; ++i)
                frames.append(i);
        } else {
            const int row = qMax(0, animation.value("row").toInt(0));
            const int count = qBound(1, configuredFrames.toInt(1), 256);
            const int columns = qMax(1, spriteSheet_.width() / frameWidth_);
            for (int i = 0; i < count; ++i)
                frames.append(row * columns + i);
        }
        const int columns = qMax(1, spriteSheet_.width() / frameWidth_);
        if (!frames.isEmpty()) {
            const int index = frames.at(frame_ % frames.size());
            const QRect source((index % columns) * frameWidth_, (index / columns) * frameHeight_,
                               frameWidth_, frameHeight_);
            if (source.right() <= spriteSheet_.width() && source.bottom() <= spriteSheet_.height()) {
                painter.save();
                const bool faceLeft = lookAtMouse_ ? faceLeft_ :
                    (movementAnimationActive_ && movementFacingLeft_);
                if (faceLeft) {
                    painter.translate(bounds.left() + bounds.right(), 0);
                    painter.scale(-1, 1);
                }
                painter.drawImage(bounds, spriteSheet_, source);
                painter.restore();
            } else {
                drawFallbackPet(painter, bounds);
            }
        } else {
            drawFallbackPet(painter, bounds);
        }
    } else {
        drawFallbackPet(painter, bounds);
    }
}

void PetWindow::drawFallbackPet(QPainter &painter, const QRect &bounds) {
    const qreal bob = animation_ == "sleep" ? 0.0 : qSin(frame_ * 0.18) * 3.0;
    QRectF body(bounds.x() + 28, bounds.y() + 31 + bob, bounds.width() - 56, bounds.height() - 34);
    QColor fur(247, 185, 111);
    if (animation_ == "error") fur = QColor(235, 117, 115);
    if (animation_ == "done") fur = QColor(128, 207, 158);
    if (animation_ == "thinking" || animation_ == "review") fur = QColor(145, 190, 231);
    if (animation_ == "tool" || animation_ == "work") fur = QColor(238, 202, 115);
    if (animation_ == "cancelled") fur = QColor(173, 164, 189);

    painter.setPen(QPen(QColor(79, 67, 61), 3));
    painter.setBrush(fur);
    QPainterPath ears;
    ears.moveTo(body.left() + 7, body.top() + 17);
    ears.lineTo(body.left() + 1, body.top() - 15);
    ears.lineTo(body.left() + 27, body.top() + 7);
    ears.moveTo(body.right() - 7, body.top() + 17);
    ears.lineTo(body.right() - 1, body.top() - 15);
    ears.lineTo(body.right() - 27, body.top() + 7);
    painter.drawPath(ears);
    painter.drawEllipse(body);

    const qreal eyeY = body.top() + body.height() * 0.42 + (lookAtMouse_ ? lookOffset_.y() * 2.0 : 0.0);
    qreal eyeOffset = lookAtMouse_ ? lookOffset_.x() * 3.0 : 0.0;
    if (animation_ == "thinking") eyeOffset = -3.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(52, 48, 50));
    if (animation_ != "sleep" && animation_ != "cancelled") {
        painter.drawEllipse(QPointF(body.left() + body.width() * 0.34 + eyeOffset, eyeY), 3.2, 4.2);
        painter.drawEllipse(QPointF(body.left() + body.width() * 0.66 + eyeOffset, eyeY), 3.2, 4.2);
    } else {
        painter.setPen(QPen(QColor(52, 48, 50), 2));
        painter.drawLine(QPointF(body.left() + body.width() * .27, eyeY), QPointF(body.left() + body.width() * .41, eyeY));
        painter.drawLine(QPointF(body.left() + body.width() * .59, eyeY), QPointF(body.left() + body.width() * .73, eyeY));
    }
    painter.setPen(QPen(QColor(52, 48, 50), 2));
    painter.drawArc(QRectF(body.center().x() - 5, eyeY + 8, 10, 7), 200 * 16, 140 * 16);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 144, 148, 180));
    painter.drawEllipse(QPointF(body.left() + body.width() * .2, eyeY + 12), 5, 3);
    painter.drawEllipse(QPointF(body.right() - body.width() * .2, eyeY + 12), 5, 3);

    if (animation_ == "thinking") {
        painter.setPen(QPen(QColor(110, 155, 209), 3));
        painter.drawEllipse(QPointF(body.right() + 5, body.top() + 3), 5, 5);
        painter.drawEllipse(QPointF(body.right() + 14, body.top() - 8), 8, 8);
    } else if (animation_ == "sleep") {
        painter.setPen(QColor(103, 115, 155));
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 13, QFont::Bold));
        painter.drawText(QPointF(body.right() - 5, body.top() - 1), QStringLiteral("Zz"));
    } else if (animation_ == "work" || animation_ == "tool" || animation_ == "review") {
        painter.setPen(QPen(QColor(90, 104, 122), 3));
        painter.drawLine(QPointF(body.left() + 5, body.bottom() - 5), QPointF(body.right() - 5, body.bottom() - 5));
    }
}

void PetWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        dragOffset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        pressPosition_ = event->globalPosition().toPoint();
        dragging_ = false;
        dragSamples_.clear();
        dragVelocityTimer_.start();
        dragSamples_.append({0, pressPosition_});
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PetWindow::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) {
        const QPoint globalPosition = event->globalPosition().toPoint();
        if ((globalPosition - pressPosition_).manhattanLength() > 4 && !dragging_) {
            dragging_ = true;
            detachFromWindow();
            cancelRest();
            stopFreeMovement();
            stopGravity();
            dragVelocityTimer_.restart();
            dragSamples_.clear();
            dragSamples_.append({0, globalPosition});
        }
        if (dragging_) {
            dragSamples_.append({dragVelocityTimer_.elapsed(), globalPosition});
            const qint64 nowMs = dragSamples_.last().timeMs;
            while (dragSamples_.size() > 2 && nowMs - dragSamples_.first().timeMs > 250)
                dragSamples_.removeFirst();
            if (dragSamples_.size() > 64)
                dragSamples_.remove(0, dragSamples_.size() - 64);
            move(boundedPosition(globalPosition - dragOffset_, globalPosition));
        }
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

QPoint PetWindow::boundedPosition(const QPoint &position, const QPoint &referencePoint) const {
    QScreen *screen = QGuiApplication::screenAt(referencePoint);
    if (!screen)
        screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return position;
    const QRect area = screen->availableGeometry();
    return {qBound(area.left(), position.x(), qMax(area.left(), area.right() - width() + 1)),
            qBound(area.top(), position.y(), qMax(area.top(), area.bottom() - height() + 1))};
}

QPointF PetWindow::dragReleaseVelocity(const QPoint &releasePosition) const {
    if (dragSamples_.isEmpty() || !dragVelocityTimer_.isValid())
        return {};

    const qint64 nowMs = dragVelocityTimer_.elapsed();
    const DragSample *anchor = &dragSamples_.constFirst();
    for (int i = dragSamples_.size() - 1; i >= 0; --i) {
        if (nowMs - dragSamples_.at(i).timeMs >= flingSampleWindowMs) {
            anchor = &dragSamples_.at(i);
            break;
        }
    }

    const qint64 elapsedMs = nowMs - anchor->timeMs;
    if (elapsedMs < 10)
        return {};

    QPointF velocity = QPointF(releasePosition - anchor->position) * (16.0 / qreal(elapsedMs));
    const qreal speed = qSqrt(velocity.x() * velocity.x() + velocity.y() * velocity.y());
    if (speed < minFlingVelocity)
        return {};
    if (speed > maxFlingVelocity)
        velocity *= maxFlingVelocity / speed;
    return velocity;
}

void PetWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && !dragging_) {
        emit petClicked();
        event->accept();
        return;
    }
    const bool wasDragging = dragging_;
    const QPointF flingVelocity = wasDragging
        ? dragReleaseVelocity(event->globalPosition().toPoint()) : QPointF();
    dragging_ = false;
    dragSamples_.clear();
    QWidget::mouseReleaseEvent(event);
    if (!wasDragging)
        return;
    if (tryAttachToWindow(event->globalPosition().toPoint(), flingVelocity))
        return;
    if (doNotDisturb_) {
        dockToNearestEdge();
        return;
    }
    if (gravityEnabled_)
        startGravity(flingVelocity);
    else if (freeMovementEnabled_)
        scheduleFreeMovement(QRandomGenerator::global()->bounded(1500, 4201));
    if (animation_ == QStringLiteral("idle"))
        scheduleRest();
}

void PetWindow::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        emit chatRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void PetWindow::contextMenuEvent(QContextMenuEvent *event) {
    showContextMenu(event->globalPos());
    event->accept();
}

void PetWindow::showContextMenu(const QPoint &globalPosition) {
    QMenu menu(this);
    QMenu *animationMenu = menu.addMenu(tr("Animation"));
    QStringList animationNames = builtinAnimations;
    for (const QString &name : definition_.value("animations").toObject().keys()) {
        if (!animationNames.contains(name)) animationNames.append(name);
    }
    const QHash<QString, QString> translatedAnimations = {
        {QStringLiteral("idle"), tr("Idle")}, {QStringLiteral("walk"), tr("Walk")},
        {QStringLiteral("sleep"), tr("Sleep")}, {QStringLiteral("thinking"), tr("Thinking")},
        {QStringLiteral("work"), tr("Working")}, {QStringLiteral("review"), tr("Review")},
        {QStringLiteral("error"), tr("Error")}, {QStringLiteral("done"), tr("Completed")},
        {QStringLiteral("cancelled"), tr("Cancelled")}, {QStringLiteral("tool"), tr("Using tools")},
        {QStringLiteral("interact"), tr("Interact")}, {QStringLiteral("permission"), tr("Permission request")}
    };
    for (const QString &name : animationNames) {
        QAction *action = animationMenu->addAction(translatedAnimations.value(name, name));
        connect(action, &QAction::triggered, this, [this, name] { setAnimation(name); });
    }
    menu.addAction(tr("Say something…"), this, &PetWindow::chatRequested);
    if (!randomQuote().isEmpty())
        menu.addAction(tr("Poke for a random line"), this, [this] { setSpeech(randomQuote()); });
    QMenu *libraryMenu = menu.addMenu(tr("Pet library"));
    const QString libraryPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("pets");
    QDir library(libraryPath);
    const QFileInfoList pets = library.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &pet : pets) {
        const QString id = pet.fileName();
        QAction *switchAction = libraryMenu->addAction(tr("Switch to %1").arg(id));
        connect(switchAction, &QAction::triggered, this, [this, id] { emit switchRequested(id); });
        QAction *previewAction = libraryMenu->addAction(tr("Preview %1").arg(id));
        connect(previewAction, &QAction::triggered, this, [this, id] { emit previewRequested(id); });
    }
    libraryMenu->addSeparator();
    libraryMenu->addAction(tr("Import config + spritesheet…"), this, &PetWindow::importRequested);
    libraryMenu->addAction(tr("Import pet folder…"), this, &PetWindow::importFolderRequested);
    libraryMenu->addAction(tr("Summon all pets"), this, &PetWindow::summonAllRequested);
    if (petId() != "default") {
        menu.addAction(tr("Delete this pet…"), this, [this] { emit deleteRequested(petId()); });
    }
    menu.addSeparator();
    menu.addAction(tr("Settings & Library…"), this, &PetWindow::settingsRequested);
    QAction *movementAction = menu.addAction(tr("Walk freely"));
    movementAction->setCheckable(true);
    movementAction->setChecked(freeMovementEnabled_);
    connect(movementAction, &QAction::toggled, this, &PetWindow::freeMovementToggled);
    QAction *gravityAction = menu.addAction(tr("Gravity"));
    gravityAction->setCheckable(true);
    gravityAction->setChecked(gravityEnabled_);
    connect(gravityAction, &QAction::toggled, this, &PetWindow::gravityToggled);
    QAction *lookAction = menu.addAction(tr("Look at the mouse cursor"));
    lookAction->setCheckable(true);
    lookAction->setChecked(lookAtMouse_);
    connect(lookAction, &QAction::toggled, this, &PetWindow::lookAtMouseToggled);
    if (attachedToWindow()) {
        menu.addAction(tr("Slide down the window frame"), this, &PetWindow::slideDownBorder);
        menu.addAction(tr("Let go of the window"), this, &PetWindow::releaseFromWindow);
    }
    QAction *quietAction = menu.addAction(tr("Do not disturb"));
    quietAction->setCheckable(true);
    quietAction->setChecked(doNotDisturb_);
    connect(quietAction, &QAction::toggled, this, &PetWindow::doNotDisturbToggled);
    menu.addAction(tr("Hide pet"), this, &QWidget::hide);
    menu.exec(globalPosition);
}
