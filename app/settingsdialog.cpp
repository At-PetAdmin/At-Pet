#include "agentregistry.h"
#include "settingsdialog.h"
#include "petwindow.h"
#include "theme.h"

#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QUrl>
#include <QtMath>

namespace {
QString petLibraryPath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("pets");
}

bool lookAtMouseSetting() {
    QSettings settings;
    if (settings.contains(QStringLiteral("pet/lookAtMouse")))
        return settings.value(QStringLiteral("pet/lookAtMouse")).toBool();
    const bool legacy = settings.value(QStringLiteral("pet/lookAtCopilot"), false).toBool();
    settings.setValue(QStringLiteral("pet/lookAtMouse"), legacy);
    settings.remove(QStringLiteral("pet/lookAtCopilot"));
    return legacy;
}

QString animationFpsKey(const QString &petId, const QString &animation) {
    return QStringLiteral("animationFps/%1/%2")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(petId)),
             QString::fromLatin1(QUrl::toPercentEncoding(animation)));
}

const QStringList builtinAnimations = {
    QStringLiteral("idle"), QStringLiteral("walk"), QStringLiteral("sleep"),
    QStringLiteral("thinking"), QStringLiteral("work"), QStringLiteral("review"),
    QStringLiteral("error"), QStringLiteral("done"), QStringLiteral("cancelled"),
    QStringLiteral("tool"), QStringLiteral("interact"), QStringLiteral("permission")
};

QString monitorTypeDisplayName(AgentMonitorType type) {
    switch (type) {
    case AgentMonitorType::ExternalHttpApi:
        return QCoreApplication::translate("SettingsDialog", "External sender required (no built-in monitor)");
    case AgentMonitorType::ClaudeCodeHttpHooks:
        return QCoreApplication::translate("SettingsDialog", "Claude Code HTTP hooks");
    case AgentMonitorType::OpenCodeV2Plugin:
        return QCoreApplication::translate("SettingsDialog", "OpenCode V2 plugin");
    }
    return QCoreApplication::translate("SettingsDialog", "External sender required (no built-in monitor)");
}

QString translatedAgentDescription(const AgentDefinition &agent) {
    const QByteArray source = agent.description.toUtf8();
    return QCoreApplication::translate("AgentRegistry", source.constData());
}

QString integrationDisplayName(AgentIntegration integration) {
    switch (integration) {
    case AgentIntegration::BuiltInAdapter:
        return QCoreApplication::translate("AgentRegistry", "Integration: built-in adapter, works as soon as the agent runs");
    case AgentIntegration::ProfileShipped:
        return QCoreApplication::translate("AgentRegistry", "Integration: bridge profile included; configure the agent to send events");
    case AgentIntegration::ExternalRequired:
        return QCoreApplication::translate("AgentRegistry", "Integration: no sender bundled; stays silent until one is configured");
    }
    return QCoreApplication::translate("AgentRegistry", "Integration: no sender bundled; stays silent until one is configured");
}

QString translatedAnimationName(const QString &name) {
    if (name == "idle") return QCoreApplication::translate("SettingsDialog", "Idle");
    if (name == "walk") return QCoreApplication::translate("SettingsDialog", "Walk");
    if (name == "sleep") return QCoreApplication::translate("SettingsDialog", "Sleep");
    if (name == "thinking") return QCoreApplication::translate("SettingsDialog", "Thinking");
    if (name == "work") return QCoreApplication::translate("SettingsDialog", "Working");
    if (name == "review") return QCoreApplication::translate("SettingsDialog", "Review");
    if (name == "error") return QCoreApplication::translate("SettingsDialog", "Error");
    if (name == "done") return QCoreApplication::translate("SettingsDialog", "Completed");
    if (name == "cancelled") return QCoreApplication::translate("SettingsDialog", "Cancelled");
    if (name == "tool") return QCoreApplication::translate("SettingsDialog", "Using tools");
    if (name == "interact") return QCoreApplication::translate("SettingsDialog", "Interact");
    if (name == "permission") return QCoreApplication::translate("SettingsDialog", "Permission");
    return name;
}

bool copyDirRecursive(const QString &sourcePath, const QString &destinationPath) {
    QDir source(sourcePath);
    if (!source.exists() || !QDir().mkpath(destinationPath))
        return false;
    const QFileInfoList entries = source.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name | QDir::DirsFirst);
    for (const QFileInfo &entry : entries) {
        if (entry.isSymLink()) continue;
        const QString destination = QDir(destinationPath).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyDirRecursive(entry.absoluteFilePath(), destination))
                return false;
        } else if (!QFile::copy(entry.absoluteFilePath(), destination)) {
            return false;
        }
    }
    return true;
}
}

// ---------------- PetPreviewWidget ----------------

PetPreviewWidget::PetPreviewWidget(QWidget *parent) : QWidget(parent) {
    setMinimumSize(220, 220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &PetPreviewWidget::tick);
    timer_.start(125); // 8 fps default
}

void PetPreviewWidget::setPetDefinition(const QJsonObject &definition, const QString &spriteSheetPath) {
    definition_ = definition;
    spriteSheetPath_ = spriteSheetPath;
    if (!spriteSheetPath_.isEmpty() && QFile::exists(spriteSheetPath_)) {
        spriteSheet_ = QImage(spriteSheetPath_);
    } else {
        spriteSheet_ = QImage();
    }

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
    fps_ = qBound(1, definition_.value("fps").toInt(8), 30);
    const double defaultScale = cell.isUndefined() ? 4.0 : 1.0;
    scale_ = qBound(0.25, definition_.value("scale").toDouble(defaultScale), 8.0);

    const QString defAnim = definition_.value("defaultAnimation").toString();
    if (!defAnim.isEmpty() && availableAnimations().contains(defAnim)) {
        currentAnimation_ = defAnim;
    } else if (!availableAnimations().contains(currentAnimation_)) {
        currentAnimation_ = QStringLiteral("idle");
    }

    const QJsonObject animation = definition_.value("animations").toObject().value(currentAnimation_).toObject();
    const int animationFps = qBound(1, animation.value("fps").toInt(fps_), 30);
    timer_.setInterval(qMax(30, 1000 / animationFps));
    frame_ = 0;
    updateFrames();
    update();
}

QStringList PetPreviewWidget::availableAnimations() const {
    QStringList list = builtinAnimations;
    for (const QString &key : definition_.value("animations").toObject().keys()) {
        if (!list.contains(key)) list.append(key);
    }
    return list;
}

void PetPreviewWidget::setAnimation(const QString &animName) {
    if (currentAnimation_ == animName) return;
    currentAnimation_ = animName;
    frame_ = 0;
    const QJsonObject animation = definition_.value("animations").toObject().value(animName).toObject();
    const int fps = qBound(1, animation.value("fps").toInt(definition_.value("fps").toInt(8)), 30);
    timer_.setInterval(qMax(30, 1000 / fps));
    updateFrames();
    update();
}

void PetPreviewWidget::setAnimationFps(int fps) {
    timer_.setInterval(qMax(30, 1000 / qBound(1, fps, 30)));
}

void PetPreviewWidget::setChrome(const QColor &cardBackground, const QColor &cardBorder,
                                 const QColor &checkerA, const QColor &checkerB) {
    cardColor_ = cardBackground;
    cardBorderColor_ = cardBorder;
    checkerA_ = checkerA;
    checkerB_ = checkerB;
    update();
}

int PetPreviewWidget::animationFps() const {
    const QJsonObject animation = definition_.value("animations").toObject().value(currentAnimation_).toObject();
    return qBound(1, animation.value("fps").toInt(definition_.value("fps").toInt(8)), 30);
}

void PetPreviewWidget::updateFrames() {
    frames_.clear();
    const QJsonObject animations = definition_.value("animations").toObject();
    const QJsonObject animation = animations.value(currentAnimation_).toObject();
    if (animation.isEmpty() || spriteSheet_.isNull()) {
        return;
    }

    const QJsonValue configuredFrames = animation.value("frames");
    if (configuredFrames.isArray()) {
        for (const QJsonValue &value : configuredFrames.toArray()) {
            if (frames_.size() == 256) break;
            frames_.append(qMax(0, value.toInt()));
        }
    } else if (animation.contains("start") || animation.contains("end")) {
        const int first = qMax(0, animation.value("start").toInt(0));
        const int last = qMax(first, animation.value("end").toInt(first));
        for (int i = first; i <= last && i - first < 256; ++i)
            frames_.append(i);
    } else {
        const int row = qMax(0, animation.value("row").toInt(0));
        const int count = qBound(1, configuredFrames.toInt(1), 256);
        const int columns = qMax(1, spriteSheet_.width() / frameWidth_);
        for (int i = 0; i < count; ++i)
            frames_.append(row * columns + i);
    }
}

void PetPreviewWidget::tick() {
    ++frame_;
    update();
}

void PetPreviewWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw checkered transparency background on the themed preview card
    painter.fillRect(rect(), cardColor_);
    painter.setPen(QPen(cardBorderColor_, 1));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 8, 8);

    // Checker pattern in the center
    const int previewSize = qMin(width() - 32, height() - 32);
    const QRect previewBox((width() - previewSize) / 2, (height() - previewSize) / 2, previewSize, previewSize);
    painter.save();
    painter.setClipRect(previewBox);

    const int gridSize = 16;
    for (int y = previewBox.top(); y < previewBox.bottom(); y += gridSize) {
        for (int x = previewBox.left(); x < previewBox.right(); x += gridSize) {
            bool odd = ((x / gridSize) + (y / gridSize)) % 2 == 1;
            painter.fillRect(QRect(x, y, gridSize, gridSize), odd ? checkerB_ : checkerA_);
        }
    }

    if (!spriteSheet_.isNull() && !frames_.isEmpty()) {
        const int columns = qMax(1, spriteSheet_.width() / frameWidth_);
        const int frameIndex = frames_.at(frame_ % frames_.size());
        const QRect source((frameIndex % columns) * frameWidth_, (frameIndex / columns) * frameHeight_,
                           frameWidth_, frameHeight_);

        // Calculate aspect fit scaled size
        const qreal targetW = frameWidth_ * scale_;
        const qreal targetH = frameHeight_ * scale_;
        qreal renderW = targetW;
        qreal renderH = targetH;
        if (renderW > previewBox.width() * 0.9 || renderH > previewBox.height() * 0.9) {
            const qreal factor = qMin((previewBox.width() * 0.9) / renderW, (previewBox.height() * 0.9) / renderH);
            renderW *= factor;
            renderH *= factor;
        }

        const QRect targetRect(previewBox.center().x() - renderW / 2,
                               previewBox.center().y() - renderH / 2,
                               renderW, renderH);

        painter.drawImage(targetRect, spriteSheet_, source);
    } else {
        // Draw fallback vector pet in preview
        const qreal bob = currentAnimation_ == "sleep" ? 0.0 : qSin(frame_ * 0.18) * 3.0;
        const int w = qMin(previewBox.width() * 0.6, 120.0);
        const int h = qMin(previewBox.height() * 0.6, 120.0);
        QRectF body(previewBox.center().x() - w / 2, previewBox.center().y() - h / 2 + bob, w, h);

        QColor fur(247, 185, 111);
        if (currentAnimation_ == "error") fur = QColor(235, 117, 115);
        if (currentAnimation_ == "done") fur = QColor(128, 207, 158);
        if (currentAnimation_ == "thinking" || currentAnimation_ == "review") fur = QColor(145, 190, 231);
        if (currentAnimation_ == "tool" || currentAnimation_ == "work") fur = QColor(238, 202, 115);
        if (currentAnimation_ == "cancelled") fur = QColor(173, 164, 189);

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
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(52, 48, 50));
        if (currentAnimation_ != "sleep" && currentAnimation_ != "cancelled") {
            painter.drawEllipse(QPointF(body.left() + body.width() * 0.34, eyeY), 3.2, 4.2);
            painter.drawEllipse(QPointF(body.left() + body.width() * 0.66, eyeY), 3.2, 4.2);
        } else {
            painter.setPen(QPen(QColor(52, 48, 50), 2));
            painter.drawLine(QPointF(body.left() + body.width() * .27, eyeY), QPointF(body.left() + body.width() * .41, eyeY));
            painter.drawLine(QPointF(body.left() + body.width() * .59, eyeY), QPointF(body.left() + body.width() * .73, eyeY));
        }
        painter.setPen(QPen(QColor(52, 48, 50), 2));
        painter.drawArc(QRectF(body.center().x() - 5, eyeY + 8, 10, 7), 200 * 16, 140 * 16);
    }
    painter.restore();
}

// ---------------- SettingsDialog ----------------

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("At-Pet Settings & Library"));
    resize(780, 600);
    setupUi();
    applyStyle();
    loadSettings();
    refreshPetsList();
}

void SettingsDialog::applyStyle() {
    const uitheme::Palette palette = uitheme::resolvePalette(uitheme::modeFromSettings());
    const auto color = [](const QColor &c) { return c.name(QColor::HexRgb); };

    previewWidget_->setChrome(palette.previewCard, palette.previewCardBorder, palette.checkerA, palette.checkerB);
    setStyleSheet(uitheme::settingsDialogStyle(palette));

    installedPetsLabel_->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 14px; color: %1;").arg(color(palette.text)));
    previewLabel_->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 14px; color: %1;").arg(color(palette.text)));
    importStatusLabel_->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; padding: 2px 4px;").arg(color(palette.statusOk)));
    animationFpsNoteLabel_->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(color(palette.textFaint)));
    agentHeaderLabel_->setStyleSheet(QStringLiteral("font-size: 13px; color: %1; margin-bottom: 8px;").arg(color(palette.textSecondary)));
    petInfoLabel_->setStyleSheet(QStringLiteral("background: %1; border: 1px solid %2; border-radius: 8px; padding: 10px; color: %3; font-size: 12px;")
                                     .arg(color(palette.surfaceAlt), color(palette.border), color(palette.textSecondary)));
    for (QLabel *label : agentDescriptionLabels_)
        label->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(color(palette.textFaint)));
}

void SettingsDialog::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(18, 18, 18, 18);
    mainLayout->setSpacing(12);

    tabWidget_ = new QTabWidget(this);
    mainLayout->addWidget(tabWidget_);

    // ---------------- TAB 1: Pet Library & Preview ----------------
    auto *petLibraryTab = new QWidget;
    auto *petLayout = new QHBoxLayout(petLibraryTab);
    petLayout->setContentsMargins(16, 16, 16, 16);
    petLayout->setSpacing(20);

    // Left column: Pet list and import actions
    auto *leftCol = new QVBoxLayout;
    installedPetsLabel_ = new QLabel(tr("Installed Pets:"), petLibraryTab);
    leftCol->addWidget(installedPetsLabel_);

    petList_ = new QListWidget(petLibraryTab);
    petList_->setUniformItemSizes(true);
    petList_->setSpacing(3);
    petList_->setToolTip(tr("Each entry shows the pet name and its library folder ID."));
    leftCol->addWidget(petList_);

    importStatusLabel_ = new QLabel(petLibraryTab);
    importStatusLabel_->setWordWrap(true);
    importStatusLabel_->hide();
    leftCol->addWidget(importStatusLabel_);

    auto *btnImportLayout = new QHBoxLayout;
    btnImportFiles_ = new QPushButton(tr("Import Config + Sheet…"), petLibraryTab);
    btnImportFolder_ = new QPushButton(tr("Import Folder…"), petLibraryTab);
    btnImportLayout->addWidget(btnImportFiles_);
    btnImportLayout->addWidget(btnImportFolder_);
    leftCol->addLayout(btnImportLayout);

    auto *btnActionLayout = new QHBoxLayout;
    btnDeletePet_ = new QPushButton(tr("Delete Pet"), petLibraryTab);
    btnDeletePet_->setObjectName("dangerBtn");
    btnEditPetJson_ = new QPushButton(tr("Edit pet.json…"), petLibraryTab);
    btnEditPetJson_->setToolTip(tr("Edit the selected pet's configuration; changes reload instantly."));
    btnActionLayout->addWidget(btnEditPetJson_);
    btnActionLayout->addWidget(btnDeletePet_);
    leftCol->addLayout(btnActionLayout);

    petLayout->addLayout(leftCol, 2);

    // Right column: Live Preview, animation selector, details and apply
    auto *rightCol = new QVBoxLayout;
    previewLabel_ = new QLabel(tr("Pet Animation Preview:"), petLibraryTab);
    rightCol->addWidget(previewLabel_);

    previewWidget_ = new PetPreviewWidget(petLibraryTab);
    rightCol->addWidget(previewWidget_, 1);

    auto *animLayout = new QHBoxLayout;
    actionPreviewLabel_ = new QLabel(tr("Action Preview:"), petLibraryTab);
    animLayout->addWidget(actionPreviewLabel_);
    animCombo_ = new QComboBox(petLibraryTab);
    animLayout->addWidget(animCombo_, 1);
    fpsLabel_ = new QLabel(tr("FPS:"), petLibraryTab);
    animLayout->addWidget(fpsLabel_);
    animFpsSpin_ = new QSpinBox(petLibraryTab);
    animFpsSpin_->setRange(1, 30);
    animFpsSpin_->setSuffix(tr(" fps"));
    animFpsSpin_->setToolTip(tr("Playback speed for this animation"));
    animLayout->addWidget(animFpsSpin_);
    rightCol->addLayout(animLayout);

    animationFpsNoteLabel_ = new QLabel(tr("The matching live animation updates immediately; other animations use this rate when they next play."), petLibraryTab);
    animationFpsNoteLabel_->setWordWrap(true);
    rightCol->addWidget(animationFpsNoteLabel_);

    petInfoLabel_ = new QLabel(petLibraryTab);
    petInfoLabel_->setWordWrap(true);
    rightCol->addWidget(petInfoLabel_);

    btnApplyPet_ = new QPushButton(tr("Set as Current Pet"), petLibraryTab);
    btnApplyPet_->setObjectName("primaryBtn");
    rightCol->addWidget(btnApplyPet_);

    petLayout->addLayout(rightCol, 3);
    tabWidget_->addTab(petLibraryTab, tr("Pet Library & Preview"));

    // ---------------- TAB 2: Agent Monitoring Switches ----------------
    auto *agentsTab = new QWidget;
    auto *agentsLayout = new QVBoxLayout(agentsTab);
    agentsLayout->setContentsMargins(20, 20, 20, 20);
    agentsLayout->setSpacing(12);

    agentHeaderLabel_ = new QLabel(tr("Only Claude Code and OpenCode have built-in adapters. Other sources need an external integration to send events to At-Pet."), agentsTab);
    agentHeaderLabel_->setWordWrap(true);
    agentsLayout->addWidget(agentHeaderLabel_);

    auto *agentScrollArea = new QScrollArea(agentsTab);
    agentScrollArea->setWidgetResizable(true);
    agentScrollArea->setFrameShape(QFrame::NoFrame);
    agentScrollArea->viewport()->setAutoFillBackground(false);
    agentGroupBox_ = new QGroupBox(tr("Agent Event Source Filters"), agentScrollArea);
    agentGroupBox_->setAutoFillBackground(false);
    auto *groupLayout = new QVBoxLayout(agentGroupBox_);
    groupLayout->setSpacing(12);

    for (const AgentDefinition &agent : agentDefinitions()) {
        auto *itemWidget = new QWidget(agentGroupBox_);
        auto *itemLayout = new QHBoxLayout(itemWidget);
        itemLayout->setContentsMargins(0, 0, 0, 0);

        auto *chk = new QCheckBox(agent.displayName, itemWidget);
        chk->setChecked(true);
        agentCheckboxes_.insert(agent.key, chk);

        auto *descLabel = new QLabel(itemWidget);
        descLabel->setWordWrap(true);
        agentDescriptionLabels_.insert(agent.key, descLabel);

        itemLayout->addWidget(chk);
        itemLayout->addSpacing(16);
        itemLayout->addWidget(descLabel, 1);
        groupLayout->addWidget(itemWidget);
    }
    updateAgentDescriptions();
    groupLayout->addStretch(1);
    agentScrollArea->setWidget(agentGroupBox_);
    agentsLayout->addWidget(agentScrollArea, 1);

    subagentPetCheck_ = new QCheckBox(tr("Show a separate pet for each subagent session reported by incoming events"), agentsTab);
    agentsLayout->addWidget(subagentPetCheck_);

    btnSaveAgents_ = new QPushButton(tr("Save Event Filters"), agentsTab);
    btnSaveAgents_->setObjectName("primaryBtn");
    connect(btnSaveAgents_, &QPushButton::clicked, this, &SettingsDialog::saveSettings);
    agentsLayout->addWidget(btnSaveAgents_, 0, Qt::AlignRight);

    tabWidget_->addTab(agentsTab, tr("Agent Monitoring"));

    // ---------------- TAB 3: General Settings ----------------
    auto *generalTab = new QWidget;
    auto *generalLayout = new QVBoxLayout(generalTab);
    generalLayout->setContentsMargins(20, 20, 20, 20);
    generalLayout->setSpacing(12);

    auto *uiGroup = new QGroupBox(tr("Preferences"), generalTab);
    auto *uiForm = new QGridLayout(uiGroup);
    uiForm->setSpacing(14);
    uiForm->setColumnStretch(1, 1);

    preferencesGroupBox_ = uiGroup;
    languageLabel_ = new QLabel(tr("Interface Language:"), uiGroup);
    uiForm->addWidget(languageLabel_, 0, 0);
    languageCombo_ = new QComboBox(uiGroup);
    languageCombo_->addItem(tr("Simplified Chinese"), "zh_CN");
    languageCombo_->addItem(tr("English"), "en");
    uiForm->addWidget(languageCombo_, 0, 1);

    themeLabel_ = new QLabel(tr("Interface Theme:"), uiGroup);
    uiForm->addWidget(themeLabel_, 1, 0);
    themeCombo_ = new QComboBox(uiGroup);
    themeCombo_->addItem(tr("Follow System"), QStringLiteral("system"));
    themeCombo_->addItem(tr("Light"), QStringLiteral("light"));
    themeCombo_->addItem(tr("Dark"), QStringLiteral("dark"));
    uiForm->addWidget(themeCombo_, 1, 1);

    lookAtMouseCheck_ = new QCheckBox(tr("Eyes follow the mouse cursor (Windows)"), uiGroup);
    uiForm->addWidget(lookAtMouseCheck_, 2, 0, 1, 2);

    autoDndCheck_ = new QCheckBox(tr("Do not disturb automatically in full-screen apps (shrink to the screen edge)"), uiGroup);
    autoDndCheck_->setToolTip(tr("When a full-screen game or editor is in the foreground, the pet shrinks into a small\nbubble at the screen edge. Summon it again from the tray icon."));
    uiForm->addWidget(autoDndCheck_, 3, 0, 1, 2);

    gravityCheck_ = new QCheckBox(tr("Enable gravity (pet falls after being dragged)"), uiGroup);
    gravityCheck_->setObjectName(QStringLiteral("gravityCheck"));
    uiForm->addWidget(gravityCheck_, 4, 0, 1, 2);
    gravityScaleLabel_ = new QLabel(tr("Gravity strength:"), uiGroup);
    uiForm->addWidget(gravityScaleLabel_, 5, 0);
    gravityScaleSpin_ = new QDoubleSpinBox(uiGroup);
    gravityScaleSpin_->setRange(0.1, 3.0);
    gravityScaleSpin_->setSingleStep(0.1);
    gravityScaleSpin_->setDecimals(2);
    gravityScaleSpin_->setValue(1.0);
    gravityScaleSpin_->setToolTip(tr("1.00 is the default. Higher values make pets fall faster."));
    uiForm->addWidget(gravityScaleSpin_, 5, 1);
    completionSoundCheck_ = new QCheckBox(tr("Play a sound when an Agent task completes"), uiGroup);
    uiForm->addWidget(completionSoundCheck_, 6, 0, 1, 2);

    apiPortLabel_ = new QLabel(tr("Local API Port:"), uiGroup);
    uiForm->addWidget(apiPortLabel_, 7, 0);
    apiPortSpin_ = new QSpinBox(uiGroup);
    apiPortSpin_->setRange(1024, 65535);
    apiPortSpin_->setValue(8765);
    uiForm->addWidget(apiPortSpin_, 7, 1);

    generalLayout->addWidget(uiGroup);
    generalLayout->addStretch(1);

    btnSaveGeneral_ = new QPushButton(tr("Apply General Settings"), generalTab);
    btnSaveGeneral_->setObjectName("primaryBtn");
    btnSaveGeneral_->setProperty("testId", QStringLiteral("saveGeneralButton"));
    connect(btnSaveGeneral_, &QPushButton::clicked, this, &SettingsDialog::onSaveGeneralSettings);
    generalLayout->addWidget(btnSaveGeneral_, 0, Qt::AlignRight);

    tabWidget_->addTab(generalTab, tr("General Settings"));

    // Bottom close button
    auto *bottomLayout = new QHBoxLayout;
    bottomLayout->addStretch(1);
    btnClose_ = new QPushButton(tr("Close"), this);
    connect(btnClose_, &QPushButton::clicked, this, &QDialog::accept);
    bottomLayout->addWidget(btnClose_);
    mainLayout->addLayout(bottomLayout);

    // Connections
    connect(petList_, &QListWidget::currentItemChanged, this, &SettingsDialog::onPetSelected);
    connect(btnImportFiles_, &QPushButton::clicked, this, &SettingsDialog::onImportConfigAndSheet);
    connect(btnImportFolder_, &QPushButton::clicked, this, &SettingsDialog::onImportFolder);
    connect(btnDeletePet_, &QPushButton::clicked, this, &SettingsDialog::onDeletePet);
    connect(btnEditPetJson_, &QPushButton::clicked, this, &SettingsDialog::onEditPetJson);
    connect(btnApplyPet_, &QPushButton::clicked, this, &SettingsDialog::onApplyAsActivePet);
    connect(animCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0)
            onAnimationSelectionChanged(animCombo_->itemData(index).toString());
    });
    connect(animFpsSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsDialog::onAnimationFpsChanged);
    connect(languageCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsDialog::onLanguageComboChanged);
}

void SettingsDialog::loadSettings() {
    QSettings settings;
    // Seeding widgets is not user intent. QCheckBox emits clicked()/toggled() for every
    // programmatic setChecked(), so each write below is muted with a QSignalBlocker: a stray
    // emission here would be read as "the user flipped this switch" by whoever listens.

    // Agent filters: default enabled
    for (auto it = agentCheckboxes_.begin(); it != agentCheckboxes_.end(); ++it) {
        const QString settingKey = QStringLiteral("agents/monitor_") + it.key();
        const QSignalBlocker blocker(it.value());
        it.value()->setChecked(settings.value(settingKey, true).toBool());
    }
    {
        const QSignalBlocker blocker(subagentPetCheck_);
        subagentPetCheck_->setChecked(settings.value("agents/showSubagentPets", false).toBool());
    }

    // General settings
    const QString lang = settings.value("ui/language", "zh_CN").toString();
    const int langIdx = languageCombo_->findData(lang);
    if (langIdx >= 0) languageCombo_->setCurrentIndex(langIdx);

    const QString theme = settings.value("ui/theme", QStringLiteral("system")).toString();
    const int themeIdx = themeCombo_->findData(theme);
    if (themeIdx >= 0) themeCombo_->setCurrentIndex(themeIdx);

    {
        const QSignalBlocker blocker(lookAtMouseCheck_);
        lookAtMouseCheck_->setChecked(lookAtMouseSetting());
    }
    {
        const QSignalBlocker blocker(autoDndCheck_);
        autoDndCheck_->setChecked(settings.value("pet/autoDoNotDisturb", true).toBool());
    }
    {
        const QSignalBlocker blocker(gravityCheck_);
        gravityCheck_->setChecked(settings.value("pet/gravity", false).toBool());
    }
    {
        const QSignalBlocker blocker(gravityScaleSpin_);
        gravityScaleSpin_->setValue(settings.value("pet/gravityScale", 1.0).toDouble());
    }
    {
        const QSignalBlocker blocker(completionSoundCheck_);
        completionSoundCheck_->setChecked(settings.value("feedback/completionSound", true).toBool());
    }

    QString portText = QString::fromLocal8Bit(qgetenv("AT_PET_PORT"));
    quint16 defaultPort = 8765;
    if (!portText.isEmpty()) defaultPort = quint16(portText.toUInt());
    const QSignalBlocker portBlocker(apiPortSpin_);
    apiPortSpin_->setValue(settings.value("api/port", defaultPort).toUInt());
}

void SettingsDialog::setGravityEnabled(bool enabled) {
    if (!gravityCheck_)
        return;
    // QSignalBlocker keeps this push from re-emitting toggled()/gravityToggled() and
    // bouncing the change straight back to the controller.
    const QSignalBlocker blocker(gravityCheck_);
    gravityCheck_->setChecked(enabled);
}

void SettingsDialog::saveSettings() {
    QSettings settings;
    for (auto it = agentCheckboxes_.begin(); it != agentCheckboxes_.end(); ++it) {
        const QString settingKey = QStringLiteral("agents/monitor_") + it.key();
        settings.setValue(settingKey, it.value()->isChecked());
    }
    settings.setValue("agents/showSubagentPets", subagentPetCheck_->isChecked());
    emit agentFilterChanged();
    emit subagentPetSettingChanged(subagentPetCheck_->isChecked());
    QMessageBox::information(this, tr("Settings Saved"), tr("Agent monitoring preferences have been updated."));
}

void SettingsDialog::onSaveGeneralSettings() {
    QSettings settings;
    const QString newLang = languageCombo_->currentData().toString();
    const bool lookAtMouse = lookAtMouseCheck_->isChecked();
    const bool autoDoNotDisturb = autoDndCheck_->isChecked();
    const bool gravity = gravityCheck_->isChecked();
    const qreal gravityScale = gravityScaleSpin_->value();
    const bool completionSound = completionSoundCheck_->isChecked();
    const quint16 apiPort = quint16(apiPortSpin_->value());
    settings.setValue("ui/language", newLang);
    settings.setValue("ui/theme", themeCombo_->currentData().toString());
    // pet/gravity, pet/gravityScale and feedback/completionSound are persisted by the
    // controller (the single source of truth) when it handles the signals below; writing
    // them here as well would desync the two the next time the dialog is constructed.
    settings.setValue("pet/lookAtMouse", lookAtMouse);
    settings.setValue("pet/autoDoNotDisturb", autoDoNotDisturb);
    settings.setValue("api/port", apiPort);

    emit languageChanged(newLang);
    emit lookAtMouseToggled(lookAtMouse);
    emit autoDoNotDisturbToggled(autoDoNotDisturb);
    emit gravityToggled(gravity);
    emit gravityScaleChanged(gravityScale);
    emit completionSoundToggled(completionSound);
    emit apiPortChanged(apiPort);

    applyStyle();

    QMessageBox::information(this, tr("Settings Applied"), tr("General settings have been applied."));
}

void SettingsDialog::onLanguageComboChanged(int) {
    // Will be applied when clicking Apply General Settings or immediately
}

void SettingsDialog::refreshPetsList() {
    petList_->clear();

    // Built-in default pet
    auto *defaultItem = new QListWidgetItem(tr("At-Pet (Built-in Default)"), petList_);
    defaultItem->setData(Qt::UserRole, QStringLiteral("default"));

    // Library pets
    QDir library(petLibraryPath());
    const QFileInfoList pets = library.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QSettings settings;
    const QString currentSelected = settings.value("pet/selected").toString();

    QListWidgetItem *selectedItem = defaultItem;
    for (const QFileInfo &pet : pets) {
        const QString id = pet.fileName();
        const QString configPath = QDir(pet.absoluteFilePath()).filePath("pet.json");
        if (QFileInfo::exists(configPath)) {
            QString name = id;
            QFile f(configPath);
            if (f.open(QIODevice::ReadOnly)) {
                QJsonDocument d = QJsonDocument::fromJson(f.readAll());
                if (d.isObject() && !d.object().value("name").toString().isEmpty()) {
                    name = d.object().value("name").toString();
                }
            }
            auto *item = new QListWidgetItem(QStringLiteral("%1 (%2)").arg(name, id), petList_);
            item->setData(Qt::UserRole, id);
            if (id == currentSelected) selectedItem = item;
        }
    }

    petList_->setCurrentItem(selectedItem);
}

QString SettingsDialog::currentSelectedPetId() const {
    auto *item = petList_->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void SettingsDialog::onPetSelected(QListWidgetItem *current, QListWidgetItem *) {
    if (!current) return;
    const QString id = current->data(Qt::UserRole).toString();
    btnDeletePet_->setEnabled(id != "default");
    btnEditPetJson_->setEnabled(!id.isEmpty() && id != "default");
    petInfoBaseText_ = tr("Pet details are unavailable.");

    if (id == "default" || id.isEmpty()) {
        const QString dir = PetWindow::builtinPetDirectory();
        QFile f(QDir(dir).filePath("pet.json"));
        QJsonObject def;
        QString sprite;
        QString spritePath;
        if (f.open(QIODevice::ReadOnly)) {
            QByteArray data = f.readAll();
            if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);
            const QJsonDocument d = QJsonDocument::fromJson(data);
            if (d.isObject()) {
                def = PetWindow::withInferredAnimations(d.object());
                sprite = def.value("sprite").toString();
                if (sprite.isEmpty()) sprite = def.value("spriteSheet").toString();
                if (!sprite.isEmpty() && QFileInfo(sprite).fileName() == sprite)
                    spritePath = QDir(dir).filePath(sprite);
            }
        }
        previewWidget_->setPetDefinition(def, spritePath);
        if (def.isEmpty()) {
            QStringList localizedAnimations;
            for (const QString &animation : previewWidget_->availableAnimations())
                localizedAnimations.append(translatedAnimationName(animation));
            petInfoBaseText_ = tr("Name: Built-in At-Pet\nType: Dynamic vector rendering\nAnimations: %1")
                                   .arg(localizedAnimations.join(QStringLiteral(", ")));
        } else {
            int frameWidth = def.value("frameWidth").toInt();
            int frameHeight = def.value("frameHeight").toInt();
            const QJsonValue cell = def.value("cell");
            if (cell.isArray() && cell.toArray().size() >= 2) {
                if (frameWidth <= 0) frameWidth = cell.toArray().at(0).toInt(128);
                if (frameHeight <= 0) frameHeight = cell.toArray().at(1).toInt(128);
            } else if (cell.isObject()) {
                if (frameWidth <= 0) frameWidth = cell.toObject().value("width").toInt(128);
                if (frameHeight <= 0) frameHeight = cell.toObject().value("height").toInt(128);
            }
            if (frameWidth <= 0) frameWidth = 128;
            if (frameHeight <= 0) frameHeight = 128;
            petInfoBaseText_ = tr("Name: %1\nID: %2\nSpritesheet: %3\nCell Size: %4x%5\nDefault FPS: %6")
                                   .arg(def.value("name").toString(QStringLiteral("At-Pet")))
                                   .arg(QStringLiteral("default"))
                                   .arg(sprite)
                                   .arg(frameWidth)
                                   .arg(frameHeight)
                                   .arg(def.value("fps").toInt(8));
        }
    } else {
        const QString dir = QDir(petLibraryPath()).filePath(id);
        const QString configPath = QDir(dir).filePath("pet.json");
        QFile f(configPath);
        if (f.open(QIODevice::ReadOnly)) {
            QByteArray data = f.readAll();
            if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);
            QJsonDocument d = QJsonDocument::fromJson(data);
            if (d.isObject()) {
                QJsonObject def = PetWindow::withInferredAnimations(d.object());
                QString sprite = def.value("sprite").toString();
                if (sprite.isEmpty()) sprite = def.value("spriteSheet").toString();
                const QString fullSpritePath = QDir(dir).filePath(sprite);
                previewWidget_->setPetDefinition(def, fullSpritePath);

                int frameWidth = def.value("frameWidth").toInt();
                int frameHeight = def.value("frameHeight").toInt();
                const QJsonValue cell = def.value("cell");
                if (cell.isArray() && cell.toArray().size() >= 2) {
                    if (frameWidth <= 0) frameWidth = cell.toArray().at(0).toInt(128);
                    if (frameHeight <= 0) frameHeight = cell.toArray().at(1).toInt(128);
                } else if (cell.isObject()) {
                    const QJsonObject cellObj = cell.toObject();
                    if (frameWidth <= 0) frameWidth = cellObj.value("width").toInt(128);
                    if (frameHeight <= 0) frameHeight = cellObj.value("height").toInt(128);
                }
                if (frameWidth <= 0) frameWidth = 128;
                if (frameHeight <= 0) frameHeight = 128;

                petInfoBaseText_ = tr("Name: %1\nID: %2\nSpritesheet: %3\nCell Size: %4x%5\nDefault FPS: %6")
                                       .arg(def.value("name").toString(id))
                                       .arg(id)
                                       .arg(sprite)
                                       .arg(frameWidth)
                                       .arg(frameHeight)
                                       .arg(def.value("fps").toInt(8));
            }
        }
    }

    // Populate animation combo
    animCombo_->blockSignals(true);
    animCombo_->clear();
    const QStringList anims = previewWidget_->availableAnimations();
    for (const QString &anim : anims)
        animCombo_->addItem(translatedAnimationName(anim), anim);
    const int selectedIndex = animCombo_->findData(previewWidget_->currentAnimation());
    if (selectedIndex >= 0)
        animCombo_->setCurrentIndex(selectedIndex);
    animCombo_->blockSignals(false);
    const QString selectedAnimation = animCombo_->currentData().toString();
    const int defaultFps = previewWidget_->animationFps();
    const int fps = QSettings().value(animationFpsKey(id, selectedAnimation), defaultFps).toInt();
    animFpsSpin_->blockSignals(true);
    animFpsSpin_->setValue(qBound(1, fps, 30));
    animFpsSpin_->blockSignals(false);
    previewWidget_->setAnimationFps(fps);
    updatePetInfoAnimationFps();
}

void SettingsDialog::onAnimationSelectionChanged(const QString &animName) {
    if (animName.isEmpty()) return;
    previewWidget_->setAnimation(animName);
    const int defaultFps = previewWidget_->animationFps();
    const int fps = QSettings().value(animationFpsKey(currentSelectedPetId(), animName), defaultFps).toInt();
    animFpsSpin_->blockSignals(true);
    animFpsSpin_->setValue(qBound(1, fps, 30));
    animFpsSpin_->blockSignals(false);
    previewWidget_->setAnimationFps(fps);
    updatePetInfoAnimationFps();
}

void SettingsDialog::onAnimationFpsChanged(int fps) {
    const QString petId = currentSelectedPetId();
    const QString animName = animCombo_->currentData().toString();
    if (petId.isEmpty() || animName.isEmpty()) return;
    QSettings settings;
    settings.setValue(animationFpsKey(petId, animName), fps);
    settings.sync();
    previewWidget_->setAnimationFps(fps);
    updatePetInfoAnimationFps();
    emit animationFpsChanged(petId, animName, fps);
}

void SettingsDialog::updatePetInfoAnimationFps() {
    if (!petInfoLabel_)
        return;
    if (petInfoBaseText_.isEmpty()) {
        petInfoLabel_->clear();
        return;
    }
    const QString animationKey = animCombo_->currentData().toString();
    if (animationKey.isEmpty()) {
        petInfoLabel_->setText(petInfoBaseText_);
        return;
    }
    petInfoLabel_->setText(petInfoBaseText_ + QLatin1Char('\n') + tr("Selected animation %1: %2 fps (used when this animation plays)")
        .arg(translatedAnimationName(animationKey))
        .arg(animFpsSpin_->value()));
}

void SettingsDialog::onApplyAsActivePet() {
    const QString id = currentSelectedPetId();
    if (id.isEmpty()) return;
    QSettings().setValue("pet/selected", id == "default" ? QString() : id);
    emit petSwitched(id);
    QMessageBox::information(this, tr("Pet Switched"), tr("Successfully switched to %1!").arg(petList_->currentItem()->text()));
}

void SettingsDialog::onImportConfigAndSheet() {
    const QString configPath = QFileDialog::getOpenFileName(this, tr("Select pet.json"), {},
                                                             tr("Pet configuration (pet.json *.json);;JSON files (*.json)"));
    if (configPath.isEmpty()) return;
    const QString spritePath = QFileDialog::getOpenFileName(this, tr("Select spritesheet"),
                                                             QFileInfo(configPath).absolutePath(),
                                                             tr("Images (*.png *.webp *.jpg *.jpeg *.bmp)"));
    if (spritePath.isEmpty()) return;

    QFile configFile(configPath);
    if (!configFile.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Invalid pet"), tr("Could not read the selected configuration."));
        return;
    }
    QByteArray configBytes = configFile.readAll();
    if (configBytes.startsWith("\xEF\xBB\xBF")) configBytes.remove(0, 3);
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(configBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(this, tr("Invalid pet"), tr("The configuration is not valid JSON."));
        return;
    }
    QJsonObject definition = document.object();
    QImage spriteImage(spritePath);
    if (spriteImage.isNull()) {
        QMessageBox::warning(this, tr("Invalid spritesheet"), tr("The selected image could not be loaded."));
        return;
    }

    int frameWidth = definition.value("frameWidth").toInt();
    int frameHeight = definition.value("frameHeight").toInt();
    const QJsonValue cellValue = definition.value("cell");
    if (cellValue.isObject()) {
        const QJsonObject cell = cellValue.toObject();
        frameWidth = cell.value("width").toInt(frameWidth);
        frameHeight = cell.value("height").toInt(frameHeight);
    } else if (cellValue.isArray() && cellValue.toArray().size() >= 2) {
        const QJsonArray cell = cellValue.toArray();
        frameWidth = cell.at(0).toInt(frameWidth);
        frameHeight = cell.at(1).toInt(frameHeight);
    }
    if (frameWidth <= 0 || frameHeight <= 0 || frameWidth > spriteImage.width() || frameHeight > spriteImage.height()) {
        QMessageBox::warning(this, tr("Invalid pet"),
                             tr("frameWidth/frameHeight (or cell.width/cell.height) must fit the selected spritesheet."));
        return;
    }

    QString baseName = definition.value("name").toString(QFileInfo(configPath).completeBaseName());
    baseName = baseName.trimmed().replace(QRegularExpression("[^A-Za-z0-9._-]"), "-");
    if (baseName.isEmpty()) baseName = QStringLiteral("pet");
    QString id = baseName;
    QString destination = QDir(petLibraryPath()).filePath(id);
    int suffix = 2;
    while (QFileInfo::exists(destination)) {
        id = baseName + QStringLiteral("-%1").arg(suffix++);
        destination = QDir(petLibraryPath()).filePath(id);
    }
    if (!QDir().mkpath(destination)) {
        QMessageBox::critical(this, tr("Import failed"), tr("Could not create the pet library entry."));
        return;
    }

    const QString spriteName = QStringLiteral("spritesheet.") + QFileInfo(spritePath).suffix().toLower();
    if (!QFile::copy(spritePath, QDir(destination).filePath(spriteName))) {
        QDir(destination).removeRecursively();
        QMessageBox::critical(this, tr("Import failed"), tr("Could not copy the spritesheet."));
        return;
    }
    definition.insert("sprite", spriteName);
    definition.remove("spriteSheet");
    if (!definition.contains("name")) definition.insert("name", baseName);
    QSaveFile output(QDir(destination).filePath("pet.json"));
    const QByteArray normalized = QJsonDocument(definition).toJson(QJsonDocument::Indented);
    if (!output.open(QIODevice::WriteOnly) || output.write(normalized) != normalized.size() || !output.commit()) {
        output.cancelWriting();
        QDir(destination).removeRecursively();
        QMessageBox::critical(this, tr("Import failed"), tr("Could not save pet.json."));
        return;
    }

    refreshPetsList();
    for (int i = 0; i < petList_->count(); ++i) {
        if (petList_->item(i)->data(Qt::UserRole).toString() == id) {
            petList_->setCurrentRow(i);
            break;
        }
    }
    QSettings().setValue("pet/selected", id);
    emit petSwitched(id);
    importStatusLabel_->setText(tr("Imported and activated: %1").arg(baseName));
    importStatusLabel_->show();
    QMessageBox::information(this, tr("Import Successful"), tr("Pet '%1' was imported and is now the active desktop pet.").arg(baseName));
}

void SettingsDialog::onImportFolder() {
    const QString source = QFileDialog::getExistingDirectory(this, tr("Import a pet folder"));
    if (source.isEmpty()) return;
    if (!QFileInfo::exists(QDir(source).filePath("pet.json"))) {
        QMessageBox::warning(this, tr("Invalid pet"), tr("The selected folder must contain pet.json."));
        return;
    }

    const QString baseName = QFileInfo(source).fileName().replace(QRegularExpression("[^A-Za-z0-9._-]"), "-");
    QString id = baseName.isEmpty() ? QStringLiteral("pet") : baseName;
    QString destination = QDir(petLibraryPath()).filePath(id);
    int suffix = 2;
    while (QFileInfo::exists(destination)) {
        id = baseName + QStringLiteral("-%1").arg(suffix++);
        destination = QDir(petLibraryPath()).filePath(id);
    }
    if (!copyDirRecursive(source, destination)) {
        QDir(destination).removeRecursively();
        QMessageBox::critical(this, tr("Import failed"), tr("Could not copy the pet folder."));
        return;
    }

    refreshPetsList();
    for (int i = 0; i < petList_->count(); ++i) {
        if (petList_->item(i)->data(Qt::UserRole).toString() == id) {
            petList_->setCurrentRow(i);
            break;
        }
    }
    QSettings().setValue("pet/selected", id);
    emit petSwitched(id);
    importStatusLabel_->setText(tr("Imported and activated: %1").arg(baseName));
    importStatusLabel_->show();
    QMessageBox::information(this, tr("Import Successful"), tr("Pet folder '%1' was imported and is now the active desktop pet.").arg(baseName));
}

void SettingsDialog::onDeletePet() {
    const QString id = currentSelectedPetId();
    if (id.isEmpty() || id == "default") return;

    const auto answer = QMessageBox::question(this, tr("Delete pet"),
        tr("Remove %1 from your pet library? This cannot be undone.").arg(id));
    if (answer != QMessageBox::Yes) return;

    QSettings settings;
    if (settings.value("pet/selected").toString() == id) {
        settings.setValue("pet/selected", QString());
        emit petSwitched(QStringLiteral("default"));
    }
    QDir(QDir(petLibraryPath()).filePath(id)).removeRecursively();
    refreshPetsList();
}

void SettingsDialog::onEditPetJson() {
    const QString id = currentSelectedPetId();
    if (id.isEmpty() || id == "default") return;
    const QString path = QDir(QDir(petLibraryPath()).filePath(id)).filePath("pet.json");
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Edit pet.json"),
            tr("Could not open %1 for editing.").arg(path));
        return;
    }
    QString text = QString::fromUtf8(source.readAll());
    source.close();
    if (text.startsWith(QChar(0xfeff))) text.remove(0, 1);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Edit %1").arg(path));
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(tr("Animations, sprite layout and random quotes (\"quotes\") are read from this file. "
                               "Saving reloads the pet instantly."), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *editor = new QPlainTextEdit(text, &dialog);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    editor->setFont(mono);
    dialog.resize(760, 560);
    layout->addWidget(editor, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, editor] {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(editor->toPlainText().toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            const QString detail = error.error == QJsonParseError::NoError
                ? tr("The configuration must be a JSON object.")
                : tr("Invalid JSON at offset %1: %2").arg(error.offset).arg(error.errorString());
            QMessageBox::warning(&dialog, tr("Edit pet.json"), detail);
            return;
        }
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted) return;

    QSaveFile target(path);
    if (!target.open(QIODevice::WriteOnly) || target.write(editor->toPlainText().toUtf8()) < 0 || !target.commit()) {
        QMessageBox::warning(this, tr("Edit pet.json"),
            tr("Could not save %1.").arg(path));
        return;
    }
    QSettings().sync();
    importStatusLabel_->setText(tr("Saved %1. The pet reloads automatically.").arg(QFileInfo(path).fileName()));
    importStatusLabel_->show();
    onPetSelected(petList_->currentItem(), nullptr);
}

void SettingsDialog::updateAgentDescriptions() {
    for (const AgentDefinition &agent : agentDefinitions()) {
        QLabel *label = agentDescriptionLabels_.value(agent.key, nullptr);
        if (label) {
            label->setText(QStringLiteral("%1\n%2\n%3")
                .arg(translatedAgentDescription(agent),
                     tr("Monitoring strategy: %1").arg(monitorTypeDisplayName(agent.monitorType)),
                     integrationDisplayName(agent.integration)));
        }
    }
}

void SettingsDialog::retranslateUi() {
    setWindowTitle(tr("At-Pet Settings & Library"));
    tabWidget_->setTabText(0, tr("Pet Library & Preview"));
    tabWidget_->setTabText(1, tr("Agent Monitoring"));
    tabWidget_->setTabText(2, tr("General Settings"));

    installedPetsLabel_->setText(tr("Installed Pets:"));
    petList_->setToolTip(tr("Each entry shows the pet name and its library folder ID."));
    btnImportFiles_->setText(tr("Import Config + Sheet…"));
    btnImportFolder_->setText(tr("Import Folder…"));
    btnDeletePet_->setText(tr("Delete Pet"));
    btnEditPetJson_->setText(tr("Edit pet.json…"));
    btnEditPetJson_->setToolTip(tr("Edit the selected pet's configuration; changes reload instantly."));
    previewLabel_->setText(tr("Pet Animation Preview:"));
    actionPreviewLabel_->setText(tr("Action Preview:"));
    fpsLabel_->setText(tr("FPS:"));
    animFpsSpin_->setSuffix(tr(" fps"));
    animFpsSpin_->setToolTip(tr("Playback speed for this animation"));
    animationFpsNoteLabel_->setText(tr("The matching live animation updates immediately; other animations use this rate when they next play."));
    btnApplyPet_->setText(tr("Set as Current Pet"));

    for (int i = 0; i < animCombo_->count(); ++i)
        animCombo_->setItemText(i, translatedAnimationName(animCombo_->itemData(i).toString()));

    agentHeaderLabel_->setText(tr("Only Claude Code and OpenCode have built-in adapters. Other sources need an external integration to send events to At-Pet."));
    agentGroupBox_->setTitle(tr("Agent Event Source Filters"));
    subagentPetCheck_->setText(tr("Show a separate pet for each subagent session reported by incoming events"));
    btnSaveAgents_->setText(tr("Save Event Filters"));

    preferencesGroupBox_->setTitle(tr("Preferences"));
    languageLabel_->setText(tr("Interface Language:"));
    languageCombo_->setItemText(0, tr("Simplified Chinese"));
    languageCombo_->setItemText(1, tr("English"));
    themeLabel_->setText(tr("Interface Theme:"));
    themeCombo_->setItemText(0, tr("Follow System"));
    themeCombo_->setItemText(1, tr("Light"));
    themeCombo_->setItemText(2, tr("Dark"));
    lookAtMouseCheck_->setText(tr("Eyes follow the mouse cursor (Windows)"));
    autoDndCheck_->setText(tr("Do not disturb automatically in full-screen apps (shrink to the screen edge)"));
    autoDndCheck_->setToolTip(tr("When a full-screen game or editor is in the foreground, the pet shrinks into a small\nbubble at the screen edge. Summon it again from the tray icon."));
    gravityScaleLabel_->setText(tr("Gravity strength:"));
    gravityScaleSpin_->setToolTip(tr("1.00 is the default. Higher values make pets fall faster."));
    apiPortLabel_->setText(tr("Local API Port:"));
    btnSaveGeneral_->setText(tr("Apply General Settings"));
    btnClose_->setText(tr("Close"));

    updateAgentDescriptions();
    refreshPetsList();
}

void SettingsDialog::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}
