#pragma once

#include <QDialog>
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
};

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

signals:
    void petSwitched(const QString &petId);
    void languageChanged(const QString &language);
    void lookAtCopilotToggled(bool enabled);
    void apiPortChanged(quint16 port);
    void agentFilterChanged();

public slots:
    void refreshPetsList();

protected:
    void changeEvent(QEvent *event) override;

private slots:
    void onPetSelected(QListWidgetItem *current, QListWidgetItem *previous);
    void onImportConfigAndSheet();
    void onImportFolder();
    void onDeletePet();
    void onApplyAsActivePet();
    void onAnimationSelectionChanged(const QString &animName);
    void onSaveGeneralSettings();
    void onLanguageComboChanged(int index);

private:
    void setupUi();
    void loadSettings();
    void saveSettings();
    void applyStyle();
    QString currentSelectedPetId() const;

    // Tabs
    QTabWidget *tabWidget_ = nullptr;

    // Pet Library & Preview tab
    QListWidget *petList_ = nullptr;
    PetPreviewWidget *previewWidget_ = nullptr;
    QComboBox *animCombo_ = nullptr;
    QLabel *petInfoLabel_ = nullptr;
    QPushButton *btnApplyPet_ = nullptr;
    QPushButton *btnImportFiles_ = nullptr;
    QPushButton *btnImportFolder_ = nullptr;
    QPushButton *btnDeletePet_ = nullptr;

    // Agent Monitoring Settings tab
    QHash<QString, QCheckBox *> agentCheckboxes_;

    // General Settings tab
    QComboBox *languageCombo_ = nullptr;
    QCheckBox *lookAtCopilotCheck_ = nullptr;
    QSpinBox *apiPortSpin_ = nullptr;
};
