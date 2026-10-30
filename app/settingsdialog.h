#pragma once

#include <QColor>
#include <QDialog>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QTimer>
#include <QVector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSpinBox;
class QTabWidget;

class PetPreviewWidget final : public QWidget {
    Q_OBJECT

public:
    explicit PetPreviewWidget(QWidget *parent = nullptr);

    void setPetDefinition(const QJsonObject &definition, const QString &spriteSheetPath);
    void setAnimation(const QString &animName);
    void setAnimationFps(int fps);
    void setChrome(const QColor &cardBackground, const QColor &cardBorder,
                   const QColor &checkerA, const QColor &checkerB);
    int animationFps() const;
    QString currentAnimation() const { return currentAnimation_; }
    QStringList availableAnimations() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void tick();
    void updateFrames();

    QJsonObject definition_;
    QString spriteSheetPath_;
    QImage spriteSheet_;
    QString currentAnimation_ = QStringLiteral("idle");
    QTimer timer_;
    int frame_ = 0;
    int frameWidth_ = 128;
    int frameHeight_ = 128;
    int fps_ = 8;
    qreal scale_ = 1.0;
    QVector<int> frames_;
    QColor cardColor_ = QColor(0xff, 0xff, 0xff);
    QColor cardBorderColor_ = QColor(0xe4, 0xe4, 0xe4);
    QColor checkerA_ = QColor(0xf0, 0xf0, 0xf0);
    QColor checkerB_ = QColor(0xe7, 0xe7, 0xe7);
};

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

signals:
    void petSwitched(const QString &petId);
    void languageChanged(const QString &language);
    void lookAtMouseToggled(bool enabled);
    void autoDoNotDisturbToggled(bool enabled);
    void gravityToggled(bool enabled);
    void gravityScaleChanged(qreal scale);
    void completionSoundToggled(bool enabled);
    void apiPortChanged(quint16 port);
    void agentFilterChanged();
    void subagentPetSettingChanged(bool enabled);
    void animationFpsChanged(const QString &petId, const QString &animation, int fps);

public slots:
    void refreshPetsList();
    // Adopt the authoritative state owned by the window that created this dialog. These never
    // emit the matching signal, so pushing state cannot loop back.
    void setGravityEnabled(bool enabled);
    void setLookAtMouseEnabled(bool enabled);
    void setLanguage(const QString &language);

protected:
    void changeEvent(QEvent *event) override;

private slots:
    void onPetSelected(QListWidgetItem *current, QListWidgetItem *previous);
    void onImportConfigAndSheet();
    void onImportFolder();
    void onDeletePet();
    void onEditPetJson();
    void onApplyAsActivePet();
    void onAnimationSelectionChanged(const QString &animName);
    void onAnimationFpsChanged(int fps);
    void onSaveGeneralSettings();
    void onLanguageComboChanged(int index);

private:
    void setupUi();
    void loadSettings();
    void saveSettings();
    void applyStyle();
    void retranslateUi();
    void updateAgentDescriptions();
    void updatePetInfoAnimationFps();
    QString currentSelectedPetId() const;

    // Tabs
    QTabWidget *tabWidget_ = nullptr;

    // Pet Library & Preview tab
    QListWidget *petList_ = nullptr;
    QLabel *installedPetsLabel_ = nullptr;
    QLabel *previewLabel_ = nullptr;
    QLabel *actionPreviewLabel_ = nullptr;
    QLabel *fpsLabel_ = nullptr;
    QLabel *animationFpsNoteLabel_ = nullptr;
    PetPreviewWidget *previewWidget_ = nullptr;
    QComboBox *animCombo_ = nullptr;
    QSpinBox *animFpsSpin_ = nullptr;
    QLabel *petInfoLabel_ = nullptr;
    QLabel *importStatusLabel_ = nullptr;
    QPushButton *btnApplyPet_ = nullptr;
    QPushButton *btnImportFiles_ = nullptr;
    QPushButton *btnImportFolder_ = nullptr;
    QPushButton *btnDeletePet_ = nullptr;
    QPushButton *btnEditPetJson_ = nullptr;
    QString petInfoBaseText_;

    // Agent Monitoring Settings tab
    QLabel *agentHeaderLabel_ = nullptr;
    QGroupBox *agentGroupBox_ = nullptr;
    QHash<QString, QCheckBox *> agentCheckboxes_;
    QHash<QString, QLabel *> agentDescriptionLabels_;
    QCheckBox *subagentPetCheck_ = nullptr;
    QPushButton *btnSaveAgents_ = nullptr;

    // General Settings tab
    QGroupBox *preferencesGroupBox_ = nullptr;
    QLabel *languageLabel_ = nullptr;
    QLabel *themeLabel_ = nullptr;
    QLabel *apiPortLabel_ = nullptr;
    QComboBox *languageCombo_ = nullptr;
    QComboBox *themeCombo_ = nullptr;
    QCheckBox *lookAtMouseCheck_ = nullptr;
    QCheckBox *autoDndCheck_ = nullptr;
    QCheckBox *gravityCheck_ = nullptr;
    QLabel *gravityScaleLabel_ = nullptr;
    QDoubleSpinBox *gravityScaleSpin_ = nullptr;
    QCheckBox *completionSoundCheck_ = nullptr;
    QSpinBox *apiPortSpin_ = nullptr;
    QPushButton *btnSaveGeneral_ = nullptr;
    QPushButton *btnClose_ = nullptr;
};
