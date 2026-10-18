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
#include <QStandardPaths>
#include <QTimer>
#include <QtMath>

namespace {
const QStringList builtinAnimations = {
    QStringLiteral("idle"), QStringLiteral("walk"), QStringLiteral("sleep"),
    QStringLiteral("thinking"), QStringLiteral("work"), QStringLiteral("review"),
    QStringLiteral("error"), QStringLiteral("done"), QStringLiteral("cancelled"),
    QStringLiteral("tool"), QStringLiteral("interact")
};
}

PetWindow::PetWindow(const QString &petDirectory, QWidget *parent)
    : QWidget(parent), directory_(petDirectory) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);
    resize(176, 176);

    animationTimer_.setTimerType(Qt::PreciseTimer);
    connect(&animationTimer_, &QTimer::timeout, this, &PetWindow::tick);
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
        QStringLiteral("tool"), QStringLiteral("interact")
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

void PetWindow::setAnimation(const QString &name) {
    if (!builtinAnimations.contains(name) && !definition_.value("animations").toObject().contains(name))
        return;
    if (animation_ == name)
        return;
    animation_ = name;
    frame_ = 0;
    emit animationChanged(animation_);
    update();
}

void PetWindow::setSpeech(const QString &text) {
    speech_ = text.left(180);
    speechTimer_.start(5000);
    update();
}

void PetWindow::setLookAtCopilot(bool enabled) {
    if (lookAtCopilot_ == enabled)
        return;
    lookAtCopilot_ = enabled;
    if (!enabled)
        faceLeft_ = false;
    update();
}

void PetWindow::setLookDirection(bool faceLeft) {
    if (faceLeft_ == faceLeft)
        return;
    faceLeft_ = faceLeft;
    update();
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
    resize(displayWidth + 16, displayHeight + 56);
    const QString defaultAnimation = definition_.value("defaultAnimation").toString();
    if (!defaultAnimation.isEmpty() && definition_.value("animations").toObject().contains(defaultAnimation))
        animation_ = defaultAnimation;
    animationTimer_.setInterval(qMax(30, 1000 / framesPerSecond_));
    watchConfiguration();
    update();
}

void PetWindow::loadSheet() {
    spriteSheet_ = QImage(spriteSheetPath_);
}

void PetWindow::tick() {
    ++frame_;
    update();
}

void PetWindow::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRect bounds = spriteSheet_.isNull()
        ? QRect(8, 20, width() - 16, height() - 24)
        : QRect(8, height() - qMin(height() - 56, qRound(frameHeight_ * scale_)) - 8,
                qMin(width() - 16, qRound(frameWidth_ * scale_)),
                qMin(height() - 56, qRound(frameHeight_ * scale_)));

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
                if (lookAtCopilot_ && faceLeft_) {
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

    if (!speech_.isEmpty()) {
        const QRect bubble(2, 1, width() - 4, 43);
        painter.setPen(QPen(QColor(75, 72, 82), 1.2));
        painter.setBrush(QColor(255, 255, 255, 245));
        painter.drawRoundedRect(bubble, 12, 12);
        painter.setPen(QColor(42, 42, 49));
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 9));
        painter.drawText(bubble.adjusted(8, 4, -8, -4), Qt::AlignCenter | Qt::TextWordWrap, speech_);
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

    const qreal eyeY = body.top() + body.height() * 0.42;
    qreal eyeOffset = lookAtCopilot_ ? (faceLeft_ ? -3.0 : 3.0) : 0.0;
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
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PetWindow::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) {
        const QPoint globalPosition = event->globalPosition().toPoint();
        if ((globalPosition - pressPosition_).manhattanLength() > 4)
            dragging_ = true;
        if (dragging_)
            move(globalPosition - dragOffset_);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void PetWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && !dragging_) {
        emit petClicked();
        event->accept();
        return;
    }
    dragging_ = false;
    QWidget::mouseReleaseEvent(event);
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
        {QStringLiteral("interact"), tr("Interact")}
    };
    for (const QString &name : animationNames) {
        QAction *action = animationMenu->addAction(translatedAnimations.value(name, name));
        connect(action, &QAction::triggered, this, [this, name] { setAnimation(name); });
    }
    menu.addAction(tr("Say something…"), this, &PetWindow::chatRequested);
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
    QAction *lookAction = menu.addAction(tr("Look at Copilot"));
    lookAction->setCheckable(true);
    lookAction->setChecked(lookAtCopilot_);
    connect(lookAction, &QAction::toggled, this, &PetWindow::lookAtToggled);
    menu.addAction(tr("Hide pet"), this, &QWidget::hide);
    menu.exec(globalPosition);
}
