#include "settingsdialog.h"
#include "petwindow.h"

#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
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
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QtMath>

namespace {
QString petLibraryPath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("pets");
}

const QStringList builtinAnimations = {
    QStringLiteral("idle"), QStringLiteral("walk"), QStringLiteral("sleep"),
    QStringLiteral("thinking"), QStringLiteral("work"), QStringLiteral("review"),
    QStringLiteral("error"), QStringLiteral("done"), QStringLiteral("cancelled"),
    QStringLiteral("tool"), QStringLiteral("interact")
};

const struct AgentDef {
    const char *key;
    const char *displayName;
    const char *description;
} kAgents[] = {
    {"githubcopilot", "GitHub Copilot", "VS Code / Visual Studio / JetBrains Copilot Chat"},
    {"zcode", "ZCode", "ZCode Agent and development assistant"},
    {"deepseekharness", "DeepSeek Harness", "DeepSeek CLI and local agent workflow harness"},
    {"opencode", "OpenCode", "Open-source terminal AI coding assistant"},
    {"antigravity", "Antigravity", "Antigravity Agent tools and IDE extensions"},
    {"commandcode", "Command Code", "CLI command automation agent"}
};

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

    timer_.setInterval(qMax(30, 1000 / fps_));
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
    updateFrames();
    update();
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

    // Draw checkered transparency background or dark card background
    painter.fillRect(rect(), QColor(32, 34, 40));
    painter.setPen(QPen(QColor(60, 64, 76), 1));
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
            painter.fillRect(QRect(x, y, gridSize, gridSize), odd ? QColor(40, 43, 52) : QColor(35, 38, 46));
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
    resize(740, 560);
    setupUi();
    applyStyle();
    loadSettings();
    refreshPetsList();
}

void SettingsDialog::applyStyle() {
    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #1e1e24; color: #f0f0f5; font-family: 'Segoe UI', 'Microsoft YaHei', sans-serif; }"
        "QTabWidget::pane { border: 1px solid #333644; background: #252834; border-radius: 6px; top: -1px; }"
        "QTabBar::tab { background: #1a1b22; color: #a4a8b8; padding: 10px 22px; border-top-left-radius: 6px; border-top-right-radius: 6px; margin-right: 4px; font-weight: bold; }"
        "QTabBar::tab:selected { background: #252834; color: #ffffff; border: 1px solid #333644; border-bottom: none; }"
        "QTabBar::tab:hover:!selected { background: #2c2f3e; color: #d0d4e4; }"
        "QGroupBox { font-weight: bold; border: 1px solid #3a3e50; border-radius: 6px; margin-top: 14px; padding-top: 18px; color: #9bb7ff; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 14px; padding: 0 5px; }"
        "QListWidget { background: #1c1e26; border: 1px solid #333644; border-radius: 6px; color: #e4e6ee; padding: 4px; }"
        "QListWidget::item { padding: 8px 12px; border-radius: 4px; margin-bottom: 2px; }"
        "QListWidget::item:hover { background: #2a2e3d; }"
        "QListWidget::item:selected { background: #3b5998; color: #ffffff; }"
        "QPushButton { background: #383c4d; border: 1px solid #4a5066; border-radius: 5px; color: #f0f2f8; padding: 7px 16px; font-weight: 500; }"
        "QPushButton:hover { background: #454a5f; border-color: #606885; }"
        "QPushButton:pressed { background: #2e3240; }"
        "QPushButton#primaryBtn { background: #3d68ff; border: 1px solid #5a80ff; font-weight: bold; }"
        "QPushButton#primaryBtn:hover { background: #527bff; }"
        "QPushButton#dangerBtn { background: #7a2a2a; border: 1px solid #a33838; }"
        "QPushButton#dangerBtn:hover { background: #923434; }"
        "QComboBox, QSpinBox { background: #1c1e26; border: 1px solid #3b3f52; border-radius: 4px; color: #f0f0f5; padding: 5px 10px; }"
        "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 22px; border-left: 1px solid #3b3f52; }"
        "QCheckBox { color: #d8dbe6; spacing: 8px; }"
        "QCheckBox::indicator { width: 18px; height: 18px; border-radius: 4px; border: 1px solid #464b5f; background: #1c1e26; }"
        "QCheckBox::indicator:checked { background: #3d68ff; border-color: #5a80ff; }"
        "QLabel { color: #d0d4e0; }"
        "QScrollBar:vertical { background: #1c1e26; width: 10px; border-radius: 5px; }"
        "QScrollBar::handle:vertical { background: #3a3e50; border-radius: 5px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background: #4e536b; }"
    ));
}

void SettingsDialog::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    tabWidget_ = new QTabWidget(this);
    mainLayout->addWidget(tabWidget_);

    // ---------------- TAB 1: Pet Library & Preview ----------------
    auto *petLibraryTab = new QWidget;
    auto *petLayout = new QHBoxLayout(petLibraryTab);
    petLayout->setContentsMargins(12, 12, 12, 12);
    petLayout->setSpacing(16);

    // Left column: Pet list and import actions
    auto *leftCol = new QVBoxLayout;
    auto *listLabel = new QLabel(tr("Installed Pets:"), petLibraryTab);
    listLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #ffffff;");
    leftCol->addWidget(listLabel);

    petList_ = new QListWidget(petLibraryTab);
    petList_->setUniformItemSizes(true);
    petList_->setSpacing(3);
    petList_->setToolTip(tr("Each entry shows the pet name and its library folder ID."));
    leftCol->addWidget(petList_);

    importStatusLabel_ = new QLabel(petLibraryTab);
    importStatusLabel_->setWordWrap(true);
    importStatusLabel_->setStyleSheet("color: #9dd6a8; font-size: 12px; padding: 2px 4px;");
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
    btnActionLayout->addWidget(btnDeletePet_);
    leftCol->addLayout(btnActionLayout);

    petLayout->addLayout(leftCol, 2);

    // Right column: Live Preview, animation selector, details and apply
    auto *rightCol = new QVBoxLayout;
    auto *previewLabel = new QLabel(tr("Pet Animation Preview:"), petLibraryTab);
    previewLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #ffffff;");
    rightCol->addWidget(previewLabel);

    previewWidget_ = new PetPreviewWidget(petLibraryTab);
    rightCol->addWidget(previewWidget_, 1);

    auto *animLayout = new QHBoxLayout;
    animLayout->addWidget(new QLabel(tr("Action Preview:"), petLibraryTab));
    animCombo_ = new QComboBox(petLibraryTab);
    animLayout->addWidget(animCombo_, 1);
    rightCol->addLayout(animLayout);

    petInfoLabel_ = new QLabel(petLibraryTab);
    petInfoLabel_->setStyleSheet("background: #1c1e26; border: 1px solid #333644; border-radius: 5px; padding: 8px; color: #a4a8b8; font-size: 12px;");
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
    agentsLayout->setContentsMargins(18, 18, 18, 18);

    auto *agentHeaderLabel = new QLabel(tr("Configure which Agent applications will trigger desktop pet animations:"), agentsTab);
    agentHeaderLabel->setStyleSheet("font-size: 13px; color: #ffffff; margin-bottom: 8px;");
    agentsLayout->addWidget(agentHeaderLabel);

    auto *agentGroup = new QGroupBox(tr("Monitored Agent Apps"), agentsTab);
    auto *groupLayout = new QVBoxLayout(agentGroup);
    groupLayout->setSpacing(12);

    for (const auto &agent : kAgents) {
        auto *itemWidget = new QWidget(agentGroup);
        auto *itemLayout = new QHBoxLayout(itemWidget);
        itemLayout->setContentsMargins(0, 0, 0, 0);

        auto *chk = new QCheckBox(agent.displayName, itemWidget);
        chk->setChecked(true);
        agentCheckboxes_.insert(QString::fromLatin1(agent.key), chk);

        auto *descLabel = new QLabel(QString::fromUtf8(agent.description), itemWidget);
        descLabel->setStyleSheet("color: #7b8094; font-size: 11px;");

        itemLayout->addWidget(chk);
        itemLayout->addSpacing(16);
        itemLayout->addWidget(descLabel, 1);

        groupLayout->addWidget(itemWidget);
    }
    agentsLayout->addWidget(agentGroup);
    agentsLayout->addStretch(1);

    auto *btnSaveAgents = new QPushButton(tr("Save Agent Preferences"), agentsTab);
    btnSaveAgents->setObjectName("primaryBtn");
    connect(btnSaveAgents, &QPushButton::clicked, this, &SettingsDialog::saveSettings);
    agentsLayout->addWidget(btnSaveAgents, 0, Qt::AlignRight);

    tabWidget_->addTab(agentsTab, tr("Agent Monitoring"));

    // ---------------- TAB 3: General Settings ----------------
    auto *generalTab = new QWidget;
    auto *generalLayout = new QVBoxLayout(generalTab);
    generalLayout->setContentsMargins(18, 18, 18, 18);

    auto *uiGroup = new QGroupBox(tr("Preferences"), generalTab);
    auto *uiForm = new QGridLayout(uiGroup);
    uiForm->setSpacing(12);

    uiForm->addWidget(new QLabel(tr("Interface Language:"), uiGroup), 0, 0);
    languageCombo_ = new QComboBox(uiGroup);
    languageCombo_->addItem(tr("Simplified Chinese (简体中文)"), "zh_CN");
    languageCombo_->addItem(tr("English"), "en");
    uiForm->addWidget(languageCombo_, 0, 1);

    lookAtCopilotCheck_ = new QCheckBox(tr("Look towards Copilot / Active Agent window (Windows)"), uiGroup);
    uiForm->addWidget(lookAtCopilotCheck_, 1, 0, 1, 2);

    uiForm->addWidget(new QLabel(tr("Local API Port:"), uiGroup), 2, 0);
    apiPortSpin_ = new QSpinBox(uiGroup);
    apiPortSpin_->setRange(1024, 65535);
    apiPortSpin_->setValue(8765);
    uiForm->addWidget(apiPortSpin_, 2, 1);

    generalLayout->addWidget(uiGroup);
    generalLayout->addStretch(1);

    auto *btnSaveGeneral = new QPushButton(tr("Apply General Settings"), generalTab);
    btnSaveGeneral->setObjectName("primaryBtn");
    connect(btnSaveGeneral, &QPushButton::clicked, this, &SettingsDialog::onSaveGeneralSettings);
    generalLayout->addWidget(btnSaveGeneral, 0, Qt::AlignRight);

    tabWidget_->addTab(generalTab, tr("General Settings"));

    // Bottom close button
    auto *bottomLayout = new QHBoxLayout;
    bottomLayout->addStretch(1);
    auto *btnClose = new QPushButton(tr("Close"), this);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    bottomLayout->addWidget(btnClose);
    mainLayout->addLayout(bottomLayout);

    // Connections
    connect(petList_, &QListWidget::currentItemChanged, this, &SettingsDialog::onPetSelected);
    connect(btnImportFiles_, &QPushButton::clicked, this, &SettingsDialog::onImportConfigAndSheet);
    connect(btnImportFolder_, &QPushButton::clicked, this, &SettingsDialog::onImportFolder);
    connect(btnDeletePet_, &QPushButton::clicked, this, &SettingsDialog::onDeletePet);
    connect(btnApplyPet_, &QPushButton::clicked, this, &SettingsDialog::onApplyAsActivePet);
    connect(animCombo_, &QComboBox::currentTextChanged, this, &SettingsDialog::onAnimationSelectionChanged);
    connect(languageCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsDialog::onLanguageComboChanged);
}

void SettingsDialog::loadSettings() {
    QSettings settings;
    // Agent filters: default enabled
    for (auto it = agentCheckboxes_.begin(); it != agentCheckboxes_.end(); ++it) {
        const QString settingKey = QStringLiteral("agents/monitor_") + it.key();
        it.value()->setChecked(settings.value(settingKey, true).toBool());
    }

    // General settings
    const QString lang = settings.value("ui/language", "zh_CN").toString();
    const int langIdx = languageCombo_->findData(lang);
    if (langIdx >= 0) languageCombo_->setCurrentIndex(langIdx);

    lookAtCopilotCheck_->setChecked(settings.value("pet/lookAtCopilot", false).toBool());

    QString portText = QString::fromLocal8Bit(qgetenv("AT_PET_PORT"));
    quint16 defaultPort = 8765;
    if (!portText.isEmpty()) defaultPort = quint16(portText.toUInt());
    apiPortSpin_->setValue(settings.value("api/port", defaultPort).toUInt());
}

void SettingsDialog::saveSettings() {
    QSettings settings;
    for (auto it = agentCheckboxes_.begin(); it != agentCheckboxes_.end(); ++it) {
        const QString settingKey = QStringLiteral("agents/monitor_") + it.key();
        settings.setValue(settingKey, it.value()->isChecked());
    }
    emit agentFilterChanged();
    QMessageBox::information(this, tr("Settings Saved"), tr("Agent monitoring preferences have been updated."));
}

void SettingsDialog::onSaveGeneralSettings() {
    QSettings settings;
    const QString newLang = languageCombo_->currentData().toString();
    settings.setValue("ui/language", newLang);
    settings.setValue("pet/lookAtCopilot", lookAtCopilotCheck_->isChecked());
    settings.setValue("api/port", apiPortSpin_->value());

    emit languageChanged(newLang);
    emit lookAtCopilotToggled(lookAtCopilotCheck_->isChecked());
    emit apiPortChanged(quint16(apiPortSpin_->value()));

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

    if (id == "default" || id.isEmpty()) {
        QJsonObject def;
        def["name"] = QStringLiteral("At-Pet");
        previewWidget_->setPetDefinition(def, QString());
        petInfoLabel_->setText(tr("Name: Built-in At-Pet\nType: Dynamic vector rendering\nAnimations: idle, walk, sleep, thinking, work, review, tool, error, done, cancelled"));
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

                petInfoLabel_->setText(tr("Name: %1\nID: %2\nSpritesheet: %3\nCell Size: %4x%5\nFPS: %6")
                                       .arg(def.value("name").toString(id))
                                       .arg(id)
                                       .arg(sprite)
                                       .arg(frameWidth)
                                       .arg(frameHeight)
                                       .arg(def.value("fps").toInt(8)));
            }
        }
    }

    // Populate animation combo
    animCombo_->blockSignals(true);
    animCombo_->clear();
    const QStringList anims = previewWidget_->availableAnimations();
    for (const QString &anim : anims) {
        animCombo_->addItem(anim);
    }
    animCombo_->setCurrentText(previewWidget_->currentAnimation());
    animCombo_->blockSignals(false);
}

void SettingsDialog::onAnimationSelectionChanged(const QString &animName) {
    if (!animName.isEmpty()) {
        previewWidget_->setAnimation(animName);
    }
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

void SettingsDialog::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange) {
        setWindowTitle(tr("At-Pet Settings & Library"));
        tabWidget_->setTabText(0, tr("Pet Library & Preview"));
        tabWidget_->setTabText(1, tr("Agent Monitoring"));
        tabWidget_->setTabText(2, tr("General Settings"));
        refreshPetsList();
    }
    QDialog::changeEvent(event);
}
