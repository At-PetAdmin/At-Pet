#include "agentregistry.h"
#include "httpapi.h"
#include "permissiondialog.h"
#include "questiondialog.h"
#include "petwindow.h"
#include "settingsdialog.h"

#include <QActionGroup>
#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QInputDialog>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QMenu>
#include <QPointer>
#include <QQueue>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QLineEdit>
#include <QLocale>
#include <QIcon>
#include <QDebug>
#include <algorithm>
#include <QScreen>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QTranslator>
#include <QVector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
QString petLibraryPath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("pets");
}

bool copyDirectory(const QString &sourcePath, const QString &destinationPath) {
    QDir source(sourcePath);
    if (!source.exists() || !QDir().mkpath(destinationPath))
        return false;
    const QFileInfoList entries = source.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name | QDir::DirsFirst);
    for (const QFileInfo &entry : entries) {
        if (entry.isSymLink())
            continue;
        const QString destination = QDir(destinationPath).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyDirectory(entry.absoluteFilePath(), destination))
                return false;
        } else if (!QFile::copy(entry.absoluteFilePath(), destination)) {
            return false;
        }
    }
    return true;
}

QString eventToken(const QString &name) {
    QString token;
    for (const QChar character : name.toLower()) {
        if (character.isLetterOrNumber())
            token.append(character);
    }
    return token;
}

QString sessionKey(const QString &agentKey, const QString &sessionId) {
    return agentKey + QChar(0x1f) + sessionId;
}

bool isPotentiallyDangerous(const QJsonObject &request) {
    if (request.value("dangerous").toBool() || request.value("isDangerous").toBool() ||
        request.value("riskLevel").toString().compare(QStringLiteral("high"), Qt::CaseInsensitive) == 0)
        return true;
    const QString tool = request.value("toolName").toString().toLower();
    const QString context = (request.value("description").toString() + QLatin1Char(' ') +
                             request.value("command").toString() + QLatin1Char(' ') +
                             request.value("input").toString()).toLower();
    if (tool.contains("shell") || tool == "bash" || tool == "powershell" || tool == "terminal" || tool == "cmd")
        return true;
    static const QStringList riskPatterns = {
        QStringLiteral("rm -rf"), QStringLiteral("remove-item"), QStringLiteral("delete"),
        QStringLiteral("drop table"), QStringLiteral("format disk"), QStringLiteral("overwrite"),
        QStringLiteral("chmod 777"), QStringLiteral("sudo ")
    };
    return std::any_of(riskPatterns.cbegin(), riskPatterns.cend(), [&context](const QString &pattern) {
        return context.contains(pattern);
    });
}

QIcon appIcon() {
    return QIcon(QStringLiteral(":/branding/logo.png"));
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

#ifdef Q_OS_WIN
QString windowClassName(HWND window) {
    wchar_t name[256]{};
    GetClassNameW(window, name, 256);
    return QString::fromWCharArray(name);
}

bool isShellSurface(HWND window) {
    static const QStringList classes = {
        QStringLiteral("Progman"), QStringLiteral("WorkerW"),
        QStringLiteral("Shell_TrayWnd"), QStringLiteral("Shell_SecondaryTrayWnd"),
        QStringLiteral("Windows.UI.Core.CoreWindow"),
        QStringLiteral("XamlExplorerHostIslandWindow"),
        QStringLiteral("MultitaskingViewFrame"), QStringLiteral("ForegroundStaging")
    };
    return classes.contains(windowClassName(window));
}

bool foregroundWindowIsFullscreen() {
    HWND window = GetForegroundWindow();
    if (!window || !IsWindowVisible(window) || IsIconic(window) || isShellSurface(window))
        return false;
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == GetCurrentProcessId())
        return false;
    RECT bounds{};
    if (!GetWindowRect(window, &bounds))
        return false;
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info))
        return false;
    const RECT monitor = info.rcMonitor;
    constexpr LONG tolerance = 3;
    return bounds.left <= monitor.left + tolerance && bounds.top <= monitor.top + tolerance &&
           bounds.right >= monitor.right - tolerance && bounds.bottom >= monitor.bottom - tolerance;
}
#endif
}

class PetController final : public QObject {
    Q_OBJECT

public:
    explicit PetController(QObject *parent = nullptr) : QObject(parent) {
        const QString savedLanguage = QSettings().value("ui/language").toString();
        const QString systemLanguage = QLocale::system().language() == QLocale::Chinese
            ? QStringLiteral("zh_CN") : QStringLiteral("en");
        setLanguage(savedLanguage == "zh_CN" || savedLanguage == "en" ? savedLanguage : systemLanguage, false);
        QDir().mkpath(petLibraryPath());
        subagentPetsEnabled_ = QSettings().value("agents/showSubagentPets", false).toBool();
        freeMovementEnabled_ = QSettings().value("pet/freeMovement", true).toBool();
        gravityEnabled_ = QSettings().value("pet/gravity", false).toBool();
        gravityScale_ = qBound(0.1, QSettings().value("pet/gravityScale", 1.0).toDouble(), 3.0);
        completionSoundEnabled_ = QSettings().value("feedback/completionSound", true).toBool();
        autoDoNotDisturbEnabled_ = QSettings().value("pet/autoDoNotDisturb", true).toBool();
        api_ = new HttpApi([this] { return stateJson(); }, this);
        connect(api_, &HttpApi::eventReceived, this, &PetController::onAgentEvent);
        connect(api_, &HttpApi::permissionRequestReceived, this, &PetController::onPermissionRequest);
        connect(api_, &HttpApi::permissionRequestCancelled, this, &PetController::onPermissionRequestCancelled);
        connect(api_, &HttpApi::questionRequestReceived, this, &PetController::onQuestionRequest);
        connect(api_, &HttpApi::questionRequestCancelled, this, &PetController::onQuestionRequestCancelled);
        connect(api_, &HttpApi::animationRequested, this, [this](const QString &name) {
            setAllAnimations(name);
        });
        connect(api_, &HttpApi::speechRequested, this, [this](const QString &text) {
            for (PetWindow *pet : visiblePets()) pet->setSpeech(text);
            publishState();
        });
        connect(api_, &HttpApi::summonRequested, this, [this](const QJsonObject &request) {
            if (request.value("all").toBool()) summonAll();
            else switchPet(request.value("petId").toString());
        });

        const QString portText = QString::fromLocal8Bit(qgetenv("AT_PET_PORT"));
        bool portOk = false;
        const uint requestedPort = portText.toUInt(&portOk);
        const quint16 savedPort = quint16(QSettings().value("api/port", 8765).toUInt());
        const quint16 port = portOk && requestedPort > 0 && requestedPort <= 65535
            ? quint16(requestedPort) : (savedPort >= 1024 ? savedPort : 8765);
        if (!api_->start(port))
            qWarning("At-Pet local API could not bind to port %u", unsigned(port));

        createTrayIcon();
        startSelectedPet();
        lookTimer_.setInterval(120);
        connect(&lookTimer_, &QTimer::timeout, this, &PetController::updateLookTarget);
        lookTimer_.start();
        fullscreenTimer_.setInterval(900);
        connect(&fullscreenTimer_, &QTimer::timeout, this, &PetController::updateFullscreenState);
        fullscreenTimer_.start();
        housekeepingTimer_.setInterval(5000);
        connect(&housekeepingTimer_, &QTimer::timeout, this, &PetController::pruneStaleSessions);
        housekeepingTimer_.start();
    }

    ~PetController() override {
        if (translatorInstalled_) QCoreApplication::removeTranslator(&translator_);
        if (tray_) tray_->setContextMenu(nullptr);
        delete trayMenu_;
    }

private:
    struct SessionState {
        QString sessionId;
        QString agentApp;
        QString agentKey;
        QString parentSessionId;
        QString animation;
        QString model;
        QString message;
        QString source;
        QDateTime updatedAt;
        quint64 sequence = 0;
        bool isSubagent = false;
        bool terminal = false;
    };

    QVector<PetWindow *> visiblePets() const {
        QVector<PetWindow *> result;
        for (const QPointer<PetWindow> &pet : pets_) {
            if (pet)
                result.append(pet);
        }
        return result;
    }

    void startSelectedPet() {
        QSettings settings;
        const QString id = settings.value("pet/selected").toString();
        const QString directory = id.isEmpty() ? QString() : QDir(petLibraryPath()).filePath(id);
        createPet(QFileInfo::exists(QDir(directory).filePath("pet.json")) ? directory : QString(), defaultPosition());
    }

    QPoint defaultPosition() const {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen)
            return {80, 80};
        const QRect area = screen->availableGeometry();
        return {area.right() - 210, area.bottom() - 210};
    }

    PetWindow *createPet(const QString &directory, const QPoint &position) {
        auto *pet = new PetWindow(directory);
        pets_.append(pet);
        pet->setLookAtMouse(lookAtMouseSetting());
        pet->setFreeMovementEnabled(freeMovementEnabled_);
        pet->setGravityScale(gravityScale_);
        pet->setGravityEnabled(gravityEnabled_);
        QPoint topLeft = position;
        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            const QRect area = screen->availableGeometry();
            topLeft.setX(qBound(area.left(), topLeft.x(), qMax(area.left(), area.right() - pet->width() + 1)));
            topLeft.setY(qBound(area.top(), topLeft.y(), qMax(area.top(), area.bottom() - pet->height() + 1)));
        }
        pet->move(topLeft);
        connect(pet, &PetWindow::animationChanged, this, [this] { publishState(); });
        connect(pet, &PetWindow::petClicked, this, [this, pet] {
            pet->setAnimation("interact");
            const QString quote = pet->randomQuote();
            pet->setSpeech(quote.isEmpty() ? tr("Hi! Double-click to chat.") : quote);
            QTimer::singleShot(1200, pet, [this, pet] {
                if (pet->animation() == "interact")
                    pet->setAnimation("idle");
                publishState();
            });
        });
        connect(pet, &PetWindow::chatRequested, this, [this, pet] {
            bool accepted = false;
            const QString text = QInputDialog::getText(pet, tr("Talk to your pet"), tr("Say something:"),
                                                       QLineEdit::Normal, {}, &accepted);
            if (accepted && !text.trimmed().isEmpty()) {
                pet->setSpeech(text.trimmed());
                pet->setAnimation("interact");
                publishState();
            }
        });
        connect(pet, &PetWindow::switchRequested, this, &PetController::switchPet);
        connect(pet, &PetWindow::importRequested, this, &PetController::importPet);
        connect(pet, &PetWindow::importFolderRequested, this, &PetController::importPetFolder);
        connect(pet, &PetWindow::summonAllRequested, this, &PetController::summonAll);
        connect(pet, &PetWindow::deleteRequested, this, &PetController::deletePet);
        connect(pet, &PetWindow::lookAtMouseToggled, pet, [pet](bool enabled) {
            QSettings().setValue(QStringLiteral("pet/lookAtMouse"), enabled);
            pet->setLookAtMouse(enabled);
        });
        connect(pet, &PetWindow::doNotDisturbToggled, this, [this](bool enabled) {
            if (syncingDoNotDisturb_) return;
            setManualDoNotDisturb(enabled);
        });
        connect(pet, &PetWindow::freeMovementToggled, this, &PetController::setFreeMovementEnabled);
        connect(pet, &PetWindow::gravityToggled, this, &PetController::setGravityEnabled);
        connect(pet, &PetWindow::settingsRequested, this, &PetController::showSettingsDialog);
        connect(pet, &PetWindow::previewRequested, this, [this](const QString &id) {
            const QString path = QDir(petLibraryPath()).filePath(id);
            if (!QFileInfo::exists(QDir(path).filePath("pet.json"))) return;
            PetWindow *preview = createPet(path, defaultPosition() - QPoint(190, 0));
            preview->setWindowTitle(tr("Preview: %1").arg(preview->petName()));
            QTimer::singleShot(12000, preview, [this, preview] {
                pets_.removeAll(preview);
                preview->close();
                preview->deleteLater();
                publishState();
            });
        });
        connect(pet, &QObject::destroyed, this, [this] { publishState(); });
        pet->show();
        applyDoNotDisturbTo(pet);
        publishState();
        return pet;
    }

    void showSettingsDialog() {
        if (!settingsDialog_) {
            settingsDialog_ = new SettingsDialog;
            // The dialog seeds its widgets from QSettings while constructing. Mute it until
            // the controller has pushed the authoritative state, so seeding can neither echo
            // back into the controller nor leave a stale copy on screen.
            const QSignalBlocker blocker(settingsDialog_);
            settingsDialog_->setAttribute(Qt::WA_DeleteOnClose);
            connect(settingsDialog_, &SettingsDialog::petSwitched, this, &PetController::switchPet);
            connect(settingsDialog_, &SettingsDialog::languageChanged, this, [this](const QString &language) {
                setLanguage(language);
            });
            connect(settingsDialog_, &SettingsDialog::lookAtMouseToggled, this, [this](bool enabled) {
                for (PetWindow *pet : visiblePets()) pet->setLookAtMouse(enabled);
            });
            connect(settingsDialog_, &SettingsDialog::autoDoNotDisturbToggled, this, [this](bool enabled) {
                autoDoNotDisturbEnabled_ = enabled;
                QSettings().setValue(QStringLiteral("pet/autoDoNotDisturb"), enabled);
                applyDoNotDisturb();
            });
            connect(settingsDialog_, &SettingsDialog::gravityToggled, this, &PetController::setGravityEnabled);
            connect(settingsDialog_, &SettingsDialog::gravityScaleChanged, this, &PetController::setGravityScale);
            connect(settingsDialog_, &SettingsDialog::completionSoundToggled, this, [this](bool enabled) {
                completionSoundEnabled_ = enabled;
                QSettings().setValue("feedback/completionSound", enabled);
            });
            connect(settingsDialog_, &SettingsDialog::apiPortChanged, this, [this](quint16 port) {
                if (api_->port() == port) return;
                QMessageBox::information(settingsDialog_, tr("Restart Required"),
                    tr("The API port change will take effect after restarting At-Pet."));
            });
            connect(settingsDialog_, &SettingsDialog::agentFilterChanged, this, [this] {
                QSettings().sync();
                refreshAgentSessions();
            });
            connect(settingsDialog_, &SettingsDialog::subagentPetSettingChanged,
                    this, &PetController::setSubagentPetsEnabled);
            connect(settingsDialog_, &SettingsDialog::animationFpsChanged, this,
                    [this](const QString &petId, const QString &animation, int fps) {
                for (PetWindow *pet : visiblePets())
                    if (pet->petId() == petId) pet->setAnimationFps(animation, fps);
            });
            connect(settingsDialog_, &QObject::destroyed, this, [this] { settingsDialog_ = nullptr; });
        }
        // Re-push on every show: the pet context menu may have changed the state while the
        // dialog existed but was hidden.
        updateSettingsCheckboxes();
        settingsDialog_->refreshPetsList();
        settingsDialog_->show();
        settingsDialog_->raise();
        settingsDialog_->activateWindow();
    }

    // Pushes controller-owned toggle state into the settings dialog. The controller (and the
    // QSettings mirror it writes) is the single source of truth; the dialog only renders it.
    void updateSettingsCheckboxes() {
        if (!settingsDialog_)
            return;
        settingsDialog_->setGravityEnabled(gravityEnabled_);
    }

    void refreshAgentSessions() {
        QSettings settings;
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            if (it->agentKey != "external" &&
                !settings.value(QStringLiteral("agents/monitor_") + it->agentKey, true).toBool())
                it = sessions_.erase(it);
            else
                ++it;
        }
        applySessionAnimation();
    }

    void setSubagentPetsEnabled(bool enabled) {
        if (subagentPetsEnabled_ == enabled)
            return;
        subagentPetsEnabled_ = enabled;
        QSettings().setValue("agents/showSubagentPets", enabled);
        applySessionAnimation();
    }

    void setFreeMovementEnabled(bool enabled) {
        if (freeMovementEnabled_ == enabled)
            return;
        freeMovementEnabled_ = enabled;
        QSettings().setValue("pet/freeMovement", enabled);
        for (PetWindow *pet : visiblePets())
            pet->setFreeMovementEnabled(enabled);
    }

    void setGravityEnabled(bool enabled) {
        if (gravityEnabled_ == enabled)
            return;
        gravityEnabled_ = enabled;
        QSettings().setValue("pet/gravity", enabled);
        for (PetWindow *pet : visiblePets())
            pet->setGravityEnabled(enabled);
        // Keep an already-open settings dialog in step with the pet context menu.
        updateSettingsCheckboxes();
    }

    void setGravityScale(qreal scale) {
        scale = qBound(0.1, scale, 3.0);
        if (qFuzzyCompare(gravityScale_, scale))
            return;
        gravityScale_ = scale;
        QSettings().setValue("pet/gravityScale", scale);
        for (PetWindow *pet : visiblePets())
            pet->setGravityScale(scale);
    }

    void updateSubagentPets() {
        QSet<QString> activeSessions;
        if (subagentPetsEnabled_) {
            int index = 0;
            const QString selectedId = QSettings().value("pet/selected").toString();
            const QString selectedDirectory = selectedId.isEmpty()
                ? QString() : QDir(petLibraryPath()).filePath(selectedId);
            const QString petDirectory = QFileInfo::exists(QDir(selectedDirectory).filePath("pet.json"))
                ? selectedDirectory : QString();

            for (auto it = sessions_.cbegin(); it != sessions_.cend(); ++it) {
                if (!it->isSubagent)
                    continue;
                activeSessions.insert(it.key());
                QPointer<PetWindow> pet = subagentPetWindows_.value(it.key());
                if (!pet) {
                    const QPoint position = defaultPosition() - QPoint(180 * (index % 3 + 1), 180 * (index / 3 + 1));
                    pet = createPet(petDirectory, position);
                    subagentPetWindows_.insert(it.key(), pet);
                }
                pet->setAnimation(it->animation);
                if (!it->message.isEmpty())
                    pet->setSpeech(it->message);
                ++index;
            }
        }

        for (auto it = subagentPetWindows_.begin(); it != subagentPetWindows_.end();) {
            if (activeSessions.contains(it.key()) && !it.value().isNull()) {
                ++it;
                continue;
            }
            if (!it.value().isNull()) {
                PetWindow *pet = it.value();
                pets_.removeAll(pet);
                pet->close();
                pet->deleteLater();
            }
            it = subagentPetWindows_.erase(it);
        }
        publishState();
    }

    void clearPets() {
        const QVector<PetWindow *> current = visiblePets();
        subagentPetWindows_.clear();
        pets_.clear();
        for (PetWindow *pet : current) {
            pet->close();
            pet->deleteLater();
        }
    }

    void switchPet(const QString &id) {
        const QString directory = id.isEmpty() || id == "default" ? QString() : QDir(petLibraryPath()).filePath(id);
        if (!directory.isEmpty() && !QFileInfo::exists(QDir(directory).filePath("pet.json")))
            return;
        QSettings().setValue("pet/selected", directory.isEmpty() ? QString() : QFileInfo(directory).fileName());
        clearPets();
        createPet(directory, defaultPosition());
        applySessionAnimation();
    }

    void importPet() {
        const QString configPath = QFileDialog::getOpenFileName(nullptr, tr("Select pet.json"), {},
                                                                 tr("Pet configuration (pet.json *.json);;JSON files (*.json)"));
        if (configPath.isEmpty()) return;
        const QString spritePath = QFileDialog::getOpenFileName(nullptr, tr("Select spritesheet"),
                                                                 QFileInfo(configPath).absolutePath(),
                                                                 tr("Images (*.png *.webp *.jpg *.jpeg *.bmp)"));
        if (spritePath.isEmpty()) return;

        QFile configFile(configPath);
        if (!configFile.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(nullptr, tr("Invalid pet"), tr("Could not read the selected configuration."));
            return;
        }
        QByteArray configBytes = configFile.readAll();
        if (configBytes.startsWith("\xEF\xBB\xBF")) configBytes.remove(0, 3);
        QJsonParseError parseError;
        QJsonDocument document = QJsonDocument::fromJson(configBytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            QMessageBox::warning(nullptr, tr("Invalid pet"), tr("The configuration is not valid JSON."));
            return;
        }
        QJsonObject definition = document.object();
        QImage spriteImage(spritePath);
        if (spriteImage.isNull()) {
            QMessageBox::warning(nullptr, tr("Invalid spritesheet"), tr("The selected image could not be loaded."));
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
            QMessageBox::warning(nullptr, tr("Invalid pet"),
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
            QMessageBox::critical(nullptr, tr("Import failed"), tr("Could not create the pet library entry."));
            return;
        }

        const QString spriteName = QStringLiteral("spritesheet.") + QFileInfo(spritePath).suffix().toLower();
        if (!QFile::copy(spritePath, QDir(destination).filePath(spriteName))) {
            QDir(destination).removeRecursively();
            QMessageBox::critical(nullptr, tr("Import failed"), tr("Could not copy the spritesheet."));
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
            QMessageBox::critical(nullptr, tr("Import failed"), tr("Could not save pet.json."));
            return;
        }
        switchPet(id);
    }

    void importPetFolder() {
        const QString source = QFileDialog::getExistingDirectory(nullptr, tr("Import a pet folder"));
        if (source.isEmpty()) return;
        if (!QFileInfo::exists(QDir(source).filePath("pet.json"))) {
            QMessageBox::warning(nullptr, tr("Invalid pet"), tr("The selected folder must contain pet.json."));
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
        if (!copyDirectory(source, destination)) {
            QDir(destination).removeRecursively();
            QMessageBox::critical(nullptr, tr("Import failed"), tr("Could not copy the pet folder."));
            return;
        }
        switchPet(id);
    }

    void summonAll() {
        releaseDoNotDisturb();
        QDir library(petLibraryPath());
        const QFileInfoList directories = library.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        QVector<QString> validPets;
        for (const QFileInfo &directory : directories) {
            if (QFileInfo::exists(QDir(directory.absoluteFilePath()).filePath("pet.json")))
                validPets.append(directory.absoluteFilePath());
        }
        if (validPets.isEmpty()) {
            switchPet(QStringLiteral("default"));
            return;
        }

        clearPets();
        QScreen *screen = QGuiApplication::primaryScreen();
        const QRect area = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
        const int columns = qMax(1, (area.width() - 20) / 180);
        for (qsizetype i = 0; i < validPets.size(); ++i) {
            const int column = int(i % columns);
            const int row = int(i / columns);
            const QPoint position(area.left() + 12 + column * 180, area.bottom() - 190 - row * 160);
            createPet(validPets.at(i), position);
        }
        applySessionAnimation();
    }

    void deletePet(const QString &id) {
        const QString directory = QDir(petLibraryPath()).filePath(id);
        const auto answer = QMessageBox::question(nullptr, tr("Delete pet"),
            tr("Remove %1 from your pet library? This cannot be undone.").arg(id));
        if (answer != QMessageBox::Yes) return;
        const QVector<PetWindow *> currentPets = visiblePets();
        if (std::any_of(currentPets.cbegin(), currentPets.cend(), [id](PetWindow *pet) { return pet->petId() == id; }))
            switchPet(QStringLiteral("default"));
        QDir(directory).removeRecursively();
        publishState();
    }

    void setAllAnimations(const QString &name) {
        for (PetWindow *pet : visiblePets()) pet->setAnimation(name);
        publishState();
    }

    void onQuestionRequest(const QString &requestId, const QJsonObject &request) {
        questionQueue_.enqueue(qMakePair(requestId, request));
        showNextQuestionDialog();
    }

    void onQuestionRequestCancelled(const QString &requestId) {
        QQueue<QPair<QString, QJsonObject>> remaining;
        while (!questionQueue_.isEmpty()) {
            const auto queued = questionQueue_.dequeue();
            if (queued.first != requestId)
                remaining.enqueue(queued);
        }
        questionQueue_ = remaining;
        if (currentQuestionRequestId_ == requestId) {
            QuestionDialog *dialog = questionDialog_.data();
            questionDialog_ = nullptr;
            currentQuestionRequestId_.clear();
            if (dialog)
                dialog->dismiss();
            QTimer::singleShot(0, this, &PetController::showNextQuestionDialog);
        }
    }

    void showNextQuestionDialog() {
        if (questionDialog_ || questionQueue_.isEmpty())
            return;
        const auto queued = questionQueue_.dequeue();
        const QString requestId = queued.first;
        const QJsonObject request = queued.second;
        QStringList choices;
        for (const QJsonValue &choice : request.value("choices").toArray())
            choices.append(choice.toString());
        PetWindow *anchor = visiblePets().isEmpty() ? nullptr : visiblePets().first();
        auto *dialog = new QuestionDialog(requestId,
            normalizeAgentApp(request.value("agentApp").toString()),
            request.value("header").toString(), request.value("question").toString(), choices, anchor);
        questionDialog_ = dialog;
        currentQuestionRequestId_ = requestId;
        connect(dialog, &QuestionDialog::answered, this, [this](const QString &id, const QString &answer) {
            if (api_)
                api_->resolveQuestion(id, true, answer);
            questionDialog_ = nullptr;
            currentQuestionRequestId_.clear();
            QTimer::singleShot(0, this, &PetController::showNextQuestionDialog);
        });
        connect(dialog, &QuestionDialog::cancelled, this, [this](const QString &id) {
            if (api_)
                api_->resolveQuestion(id, false, {});
            questionDialog_ = nullptr;
            currentQuestionRequestId_.clear();
            QTimer::singleShot(0, this, &PetController::showNextQuestionDialog);
        });
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    }

    void onAgentEvent(const QJsonObject &event) {
        const QString sessionId = event.value("sessionId").toString().trimmed();
        if (sessionId.isEmpty())
            return;
        const QString agentApp = normalizeAgentApp(event.value("agentApp").toString());
        const QString agentKey = agentAppKey(agentApp);
        const QString key = sessionKey(agentKey, sessionId);
        const QString eventName = eventToken(event.value("event").toString());
        bool successfulCompletion = false;
        if (agentKey != "external" &&
            !QSettings().value(QStringLiteral("agents/monitor_") + agentKey, true).toBool()) {
            sessions_.remove(key);
            applySessionAnimation();
            return;
        }

        QString animation;
        bool terminal = false;
        if (eventName == "thinking" || eventName == "modelthinking" || eventName == "started" ||
            eventName == "sessionstart" || eventName == "sessionstarted" || eventName == "turnstart" ||
            eventName == "turnstarted" || eventName == "userpromptsubmit" || eventName == "assistantmessage") {
            animation = "thinking";
        } else if (eventName == "toolstart" || eventName == "toolrunning" || eventName == "tool" ||
                   eventName == "tooluse" || eventName == "pretooluse" || eventName == "toolcall") {
            animation = "tool";
        } else if (eventName == "toolend" || eventName == "toolcomplete" || eventName == "toolcompleted" ||
                   eventName == "posttooluse") {
            animation = "thinking";
        } else if (eventName == "permissionrequest" || eventName == "permissionrequested" ||
                   eventName == "approvalrequest" || eventName == "approvalrequested") {
            animation = "permission";
        } else if (eventName == "permissionapproved" || eventName == "approvalapproved") {
            animation = "thinking";
        } else if (eventName == "permissiondenied" || eventName == "approvaldenied") {
            animation = "cancelled";
            terminal = true;
        } else if (eventName == "work") {
            animation = "work";
        } else if (eventName == "review" || eventName == "reflection") {
            animation = "review";
        } else if (eventName == "completed" || eventName == "completion" || eventName == "done" ||
                   eventName == "stop" || eventName == "turncomplete" || eventName == "agentturncomplete") {
            animation = "done";
            terminal = true;
            successfulCompletion = true;
        } else if (eventName == "cancelled" || eventName == "canceled" || eventName == "aborted" ||
                   eventName == "interrupt" || eventName == "interrupted") {
            animation = "cancelled";
            terminal = true;
        } else if (eventName == "error" || eventName == "failed" || eventName == "exception" ||
                   eventName == "apierror") {
            animation = "error";
            terminal = true;
        } else if (eventName == "idle" || eventName == "sessionend" || eventName == "sessionended") {
            sessions_.remove(key);
            applySessionAnimation();
            return;
        }
        if (animation.isEmpty())
            return;

        SessionState state;
        const auto previous = sessions_.constFind(key);
        successfulCompletion = successfulCompletion &&
            (previous == sessions_.cend() || !previous->terminal);
        state.sessionId = sessionId;
        state.agentApp = agentApp;
        state.agentKey = agentKey;
        state.parentSessionId = event.value("parentSessionId").toString().trimmed();
        if (state.parentSessionId.isEmpty())
            state.parentSessionId = event.value("parent_session_id").toString().trimmed();
        if (state.parentSessionId.isEmpty() && previous != sessions_.cend())
            state.parentSessionId = previous->parentSessionId;
        state.isSubagent = event.value("isSubagent").toBool() || event.value("subagent").toBool() ||
                           !state.parentSessionId.isEmpty() ||
                           (previous != sessions_.cend() && previous->isSubagent);
        state.animation = animation;
        state.model = event.value("model").toString().left(120);
        state.message = event.value("message").toString().left(180);
        state.source = event.value("source").toString().left(40);
        state.updatedAt = QDateTime::currentDateTimeUtc();
        state.sequence = ++sessionSequence_;
        state.terminal = terminal;
        sessions_.insert(key, state);
        applySessionAnimation();
        if (successfulCompletion && completionSoundEnabled_)
            QApplication::beep();
        if (terminal) {
            QTimer::singleShot(5000, this, [this, key, sequence = state.sequence] {
                const auto it = sessions_.find(key);
                if (it != sessions_.end() && it->sequence == sequence) {
                    sessions_.remove(key);
                    applySessionAnimation();
                }
            });
        }
    }

    void onPermissionRequest(const QString &requestId, const QJsonObject &request) {
        QJsonObject event = request;
        event.insert("event", "permission_requested");
        onAgentEvent(event);
        permissionQueue_.enqueue(qMakePair(requestId, request));
        showNextPermissionDialog();
    }

    void onPermissionRequestCancelled(const QString &requestId) {
        QQueue<QPair<QString, QJsonObject>> remaining;
        while (!permissionQueue_.isEmpty()) {
            const auto queued = permissionQueue_.dequeue();
            if (queued.first != requestId)
                remaining.enqueue(queued);
        }
        permissionQueue_ = remaining;

        if (currentPermissionRequestId_ == requestId) {
            PermissionDialog *dialog = permissionDialog_.data();
            permissionDialog_ = nullptr;
            currentPermissionRequestId_.clear();
            if (dialog)
                dialog->dismiss();
            QTimer::singleShot(0, this, &PetController::showNextPermissionDialog);
        }
    }

    void showNextPermissionDialog() {
        if (permissionDialog_ || permissionQueue_.isEmpty())
            return;

        const auto queued = permissionQueue_.dequeue();
        const QString requestId = queued.first;
        const QJsonObject request = queued.second;
        const QString toolName = request.value("toolName").toString();
        const QString description = request.value("description").toString();
        PetWindow *anchor = visiblePets().isEmpty() ? nullptr : visiblePets().first();
        auto *dialog = new PermissionDialog(requestId,
            normalizeAgentApp(request.value("agentApp").toString()), toolName, description,
            isPotentiallyDangerous(request), anchor);
        permissionDialog_ = dialog;
        currentPermissionRequestId_ = requestId;
        connect(dialog, &PermissionDialog::decided, this,
                [this, request](const QString &id, bool allowOnce) {
            if (api_)
                api_->resolvePermission(id, allowOnce);
            QJsonObject event = request;
            event.insert("event", allowOnce ? "permission_approved" : "permission_denied");
            onAgentEvent(event);
            permissionDialog_ = nullptr;
            currentPermissionRequestId_.clear();
            QTimer::singleShot(0, this, &PetController::showNextPermissionDialog);
        });
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    }

    void applySessionAnimation() {
        auto latest = sessions_.cend();
        for (auto it = sessions_.cbegin(); it != sessions_.cend(); ++it) {
            if (it->isSubagent)
                continue;
            if (latest == sessions_.cend() || it->updatedAt > latest->updatedAt ||
                (it->updatedAt == latest->updatedAt && it->sequence > latest->sequence))
                latest = it;
        }
        if (latest == sessions_.cend()) {
            for (auto it = sessions_.cbegin(); it != sessions_.cend(); ++it) {
                if (latest == sessions_.cend() || it->updatedAt > latest->updatedAt ||
                    (it->updatedAt == latest->updatedAt && it->sequence > latest->sequence))
                    latest = it;
            }
        }
        setAllAnimations(latest == sessions_.cend() ? QStringLiteral("idle") : latest->animation);
        if (latest != sessions_.cend() && !latest->message.isEmpty()) {
            for (PetWindow *pet : visiblePets()) pet->setSpeech(latest->message);
        }
        updateSubagentPets();
    }

    QJsonObject stateJson() const {
        QJsonArray pets;
        for (PetWindow *pet : visiblePets()) {
            pets.append(QJsonObject{{"id", pet->petId()}, {"name", pet->petName()},
                                    {"animation", pet->animation()}, {"speech", pet->speech()}});
        }
        QJsonArray sessions;
        for (auto it = sessions_.cbegin(); it != sessions_.cend(); ++it) {
            QJsonObject session{{"sessionId", it->sessionId}, {"agentApp", it->agentApp},
                                {"animation", it->animation}, {"model", it->model},
                                {"isSubagent", it->isSubagent},
                                {"updatedAt", it->updatedAt.toString(Qt::ISODate)}};
            if (!it->parentSessionId.isEmpty())
                session.insert("parentSessionId", it->parentSessionId);
            if (!it->source.isEmpty())
                session.insert("source", it->source);
            sessions.append(session);
        }
        QJsonArray supportedAgentApps;
        QJsonArray agentSources;
        for (const AgentDefinition &agent : agentDefinitions()) {
            supportedAgentApps.append(agent.displayName);
            QJsonObject source{{"key", agent.key}, {"displayName", agent.displayName},
                               {"monitorType", agentMonitorTypeId(agent.monitorType)}};
            source.insert("integration", agentIntegrationId(agent.integration));
            if (agent.monitorType != AgentMonitorType::ExternalHttpApi)
                source.insert("nativeEndpoint", QStringLiteral("/agent-event/%1").arg(agent.key));
            agentSources.append(source);
        }
        return {{"app", "At-Pet"}, {"version", AT_PET_VERSION}, {"pets", pets},
                {"sessions", sessions}, {"supportedAgentApps", supportedAgentApps},
                {"agentSources", agentSources}};
    }

    void publishState() {
        if (api_) api_->publishState();
    }

    void updateLookTarget() {
        const QPoint cursor = QCursor::pos();
        for (PetWindow *pet : visiblePets()) {
            if (!pet->isVisible() || !pet->lookAtMouse()) continue;
            pet->setLookTarget(cursor);
        }
    }

    void pruneStaleSessions() {
        const QDateTime cutoff = QDateTime::currentDateTimeUtc().addSecs(-1800);
        bool removedStaleSession = false;
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            if (it->updatedAt < cutoff) {
                it = sessions_.erase(it);
                removedStaleSession = true;
            } else {
                ++it;
            }
        }
        if (removedStaleSession)
            applySessionAnimation();
    }

    void updateFullscreenState() {
#ifdef Q_OS_WIN
        const bool fullscreen = foregroundWindowIsFullscreen();
#else
        const bool fullscreen = false;
#endif
        fullscreenTicks_ = fullscreen ? qMin(fullscreenTicks_ + 1, 2) : 0;
        const bool active = fullscreenTicks_ >= 2;
        if (active == fullscreenActive_)
            return;
        fullscreenActive_ = active;
        if (!active)
            dndSuppressed_ = false;
        applyDoNotDisturb();
    }

    bool doNotDisturbActive() const {
        return manualDoNotDisturb_ ||
            (autoDoNotDisturbEnabled_ && fullscreenActive_ && !dndSuppressed_);
    }

    void applyDoNotDisturb() {
        const bool active = doNotDisturbActive();
        if (active == doNotDisturbActive_)
            return;
        doNotDisturbActive_ = active;
        for (PetWindow *pet : visiblePets())
            applyDoNotDisturbTo(pet);
    }

    void applyDoNotDisturbTo(PetWindow *pet) {
        if (!pet) return;
        const bool previous = syncingDoNotDisturb_;
        syncingDoNotDisturb_ = true;
        pet->setDoNotDisturb(doNotDisturbActive_);
        syncingDoNotDisturb_ = previous;
    }

    void setManualDoNotDisturb(bool enabled) {
        manualDoNotDisturb_ = enabled;
        dndSuppressed_ = !enabled && fullscreenActive_;
        if (dndAction_ && dndAction_->isChecked() != enabled) {
            const QSignalBlocker blocker(dndAction_);
            dndAction_->setChecked(enabled);
        }
        applyDoNotDisturb();
    }

    void releaseDoNotDisturb() {
        manualDoNotDisturb_ = false;
        dndSuppressed_ = fullscreenActive_;
        if (dndAction_ && dndAction_->isChecked()) {
            const QSignalBlocker blocker(dndAction_);
            dndAction_->setChecked(false);
        }
        applyDoNotDisturb();
    }

    void summonPets() {
        releaseDoNotDisturb();
        for (PetWindow *pet : visiblePets()) pet->show();
        if (visiblePets().isEmpty()) startSelectedPet();
    }

    void setLanguage(const QString &language, bool persist = true) {
        if (language_ == language && (!language_.isEmpty())) return;
        if (translatorInstalled_) {
            QCoreApplication::removeTranslator(&translator_);
            translatorInstalled_ = false;
        }
        language_ = language;
        if (language_ == "zh_CN") {
            if (translator_.load(QStringLiteral(":/i18n/at-pet_zh_CN.qm"))) {
                QCoreApplication::installTranslator(&translator_);
                translatorInstalled_ = true;
            } else {
                qWarning("Could not load Simplified Chinese translations");
                language_ = QStringLiteral("en");
            }
        }
        if (persist) QSettings().setValue("ui/language", language_);
        for (PetWindow *pet : visiblePets()) {
            if (!pet->windowTitle().isEmpty())
                pet->setWindowTitle(tr("Preview: %1").arg(pet->petName()));
        }
        if (tray_) rebuildTrayMenu();
    }

    void rebuildTrayMenu() {
        tray_->setToolTip(tr("At-Pet — local agent companion"));
        QMenu *oldMenu = trayMenu_;
        trayMenu_ = new QMenu;
        trayMenu_->addAction(tr("Summon selected pet"), this, &PetController::summonPets);
        trayMenu_->addAction(tr("Summon all pets"), this, &PetController::summonAll);
        dndAction_ = trayMenu_->addAction(tr("Do not disturb"));
        dndAction_->setCheckable(true);
        dndAction_->setChecked(manualDoNotDisturb_);
        connect(dndAction_, &QAction::toggled, this, [this](bool enabled) {
            setManualDoNotDisturb(enabled);
        });
        trayMenu_->addAction(tr("Import config + spritesheet…"), this, &PetController::importPet);
        trayMenu_->addAction(tr("Import pet folder…"), this, &PetController::importPetFolder);
        trayMenu_->addAction(tr("Settings & Library…"), this, &PetController::showSettingsDialog);
        trayMenu_->addAction(tr("Hide pets"), this, [this] {
            for (PetWindow *pet : visiblePets()) pet->hide();
        });
        QMenu *languageMenu = trayMenu_->addMenu(tr("Language"));
        auto *languageGroup = new QActionGroup(languageMenu);
        languageGroup->setExclusive(true);
        QAction *chineseAction = languageMenu->addAction(tr("Simplified Chinese"));
        chineseAction->setCheckable(true);
        chineseAction->setChecked(language_ == "zh_CN");
        languageGroup->addAction(chineseAction);
        connect(chineseAction, &QAction::triggered, this, [this] { setLanguage(QStringLiteral("zh_CN")); });
        QAction *englishAction = languageMenu->addAction(tr("English"));
        englishAction->setCheckable(true);
        englishAction->setChecked(language_ == "en");
        languageGroup->addAction(englishAction);
        connect(englishAction, &QAction::triggered, this, [this] { setLanguage(QStringLiteral("en")); });
        trayMenu_->addSeparator();
        trayMenu_->addAction(tr("Quit"), qApp, &QApplication::quit);
        tray_->setContextMenu(trayMenu_);
        if (oldMenu) oldMenu->deleteLater();
    }

    void createTrayIcon() {
        tray_ = new QSystemTrayIcon(appIcon(), this);
        rebuildTrayMenu();
        tray_->show();
    }

    HttpApi *api_ = nullptr;
    QSystemTrayIcon *tray_ = nullptr;
    QMenu *trayMenu_ = nullptr;
    QTranslator translator_;
    QString language_;
    bool translatorInstalled_ = false;
    bool subagentPetsEnabled_ = false;
    bool freeMovementEnabled_ = true;
    bool gravityEnabled_ = false;
    qreal gravityScale_ = 1.0;
    bool completionSoundEnabled_ = true;
    bool autoDoNotDisturbEnabled_ = true;
    bool manualDoNotDisturb_ = false;
    bool doNotDisturbActive_ = false;
    bool fullscreenActive_ = false;
    bool dndSuppressed_ = false;
    bool syncingDoNotDisturb_ = false;
    int fullscreenTicks_ = 0;
    QAction *dndAction_ = nullptr;
    QVector<QPointer<PetWindow>> pets_;
    QHash<QString, QPointer<PetWindow>> subagentPetWindows_;
    QHash<QString, SessionState> sessions_;
    quint64 sessionSequence_ = 0;
    QQueue<QPair<QString, QJsonObject>> permissionQueue_;
    QPointer<PermissionDialog> permissionDialog_;
    QString currentPermissionRequestId_;
    QQueue<QPair<QString, QJsonObject>> questionQueue_;
    QPointer<QuestionDialog> questionDialog_;
    QString currentQuestionRequestId_;
    QTimer lookTimer_;
    QTimer fullscreenTimer_;
    QTimer housekeepingTimer_;
    QPointer<SettingsDialog> settingsDialog_;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Ni-ShuWu"));
    QCoreApplication::setApplicationName(QStringLiteral("At-Pet"));
    app.setWindowIcon(appIcon());
    QApplication::setQuitOnLastWindowClosed(false);
    PetController controller;
    return app.exec();
}

#include "main.moc"
