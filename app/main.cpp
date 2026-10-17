#include "httpapi.h"
#include "petwindow.h"

#include <QActionGroup>
#include <QApplication>
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
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QLineEdit>
#include <QLocale>
#include <QIcon>
#include <QPixmap>
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

QString normalizeAgentApp(QString name) {
    name = name.trimmed();
    if (name.isEmpty()) return QStringLiteral("external");
    QString key;
    for (const QChar character : name.toLower()) {
        if (character.isLetterOrNumber()) key.append(character);
    }
    if (key == "zcode") return QStringLiteral("ZCode");
    if (key == "githubcopilot" || key == "copilot") return QStringLiteral("GitHub Copilot");
    if (key == "deepseekharness") return QStringLiteral("DeepSeek Harness");
    if (key == "opencode") return QStringLiteral("OpenCode");
    if (key == "antigravity") return QStringLiteral("Antigravity");
    if (key == "commandcode") return QStringLiteral("Command Code");
    return name.trimmed().left(64);
}

QIcon makeTrayIcon() {
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(79, 67, 61), 3));
    painter.setBrush(QColor(247, 185, 111));
    painter.drawEllipse(QRectF(12, 16, 40, 40));
    QPainterPath ears;
    ears.moveTo(18, 25); ears.lineTo(15, 5); ears.lineTo(30, 20);
    ears.moveTo(46, 25); ears.lineTo(49, 5); ears.lineTo(34, 20);
    painter.drawPath(ears);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(52, 48, 50));
    painter.drawEllipse(QPointF(25, 34), 2.5, 3.5);
    painter.drawEllipse(QPointF(39, 34), 2.5, 3.5);
    return QIcon(pixmap);
}

#ifdef Q_OS_WIN
struct CopilotWindowInfo {
    HWND window = nullptr;
    RECT bounds{};
};

BOOL CALLBACK findCopilotWindow(HWND window, LPARAM data) {
    if (!IsWindowVisible(window))
        return TRUE;
    wchar_t title[256]{};
    GetWindowTextW(window, title, 256);
    if (QString::fromWCharArray(title).contains(QStringLiteral("Copilot"), Qt::CaseInsensitive)) {
        auto *info = reinterpret_cast<CopilotWindowInfo *>(data);
        info->window = window;
        GetWindowRect(window, &info->bounds);
        return FALSE;
    }
    return TRUE;
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
        api_ = new HttpApi([this] { return stateJson(); }, this);
        connect(api_, &HttpApi::eventReceived, this, &PetController::onAgentEvent);
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

        QString portText = QString::fromLocal8Bit(qgetenv("AT_PET_PORT"));
        bool portOk = false;
        const uint requestedPort = portText.toUInt(&portOk);
        const quint16 port = portOk && requestedPort <= 65535 ? quint16(requestedPort) : 8765;
        if (!api_->start(port))
            qWarning("At-Pet local API could not bind to port %u", unsigned(port));

        createTrayIcon();
        startSelectedPet();
        lookTimer_.setInterval(2000);
        connect(&lookTimer_, &QTimer::timeout, this, &PetController::updateLookDirection);
        lookTimer_.start();
    }

    ~PetController() override {
        if (translatorInstalled_) QCoreApplication::removeTranslator(&translator_);
        if (tray_) tray_->setContextMenu(nullptr);
        delete trayMenu_;
    }

private:
    struct SessionState {
        QString animation;
        QString agentApp;
        QString model;
        QString message;
        QDateTime updatedAt;
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
        pet->move(position);
        connect(pet, &PetWindow::animationChanged, this, [this] { publishState(); });
        connect(pet, &PetWindow::petClicked, this, [this, pet] {
            pet->setAnimation("interact");
            pet->setSpeech(tr("Hi! Double-click to chat."));
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
        connect(pet, &PetWindow::lookAtToggled, pet, &PetWindow::setLookAtCopilot);
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
        publishState();
        return pet;
    }

    void clearPets() {
        const QVector<PetWindow *> current = visiblePets();
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
        publishState();
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

    void onAgentEvent(const QJsonObject &event) {
        const QString sessionId = event.value("sessionId").toString();
        const QString eventName = event.value("event").toString().toLower();
        QString animation;
        bool terminal = false;
        if (eventName == "thinking" || eventName == "model_thinking" || eventName == "started") animation = "thinking";
        else if (eventName == "tool_start" || eventName == "tool_running" || eventName == "tool") animation = "tool";
        else if (eventName == "tool_end") animation = "thinking";
        else if (eventName == "work") animation = "work";
        else if (eventName == "review" || eventName == "reflection") animation = "review";
        else if (eventName == "completed" || eventName == "completion" || eventName == "done") { animation = "done"; terminal = true; }
        else if (eventName == "cancelled" || eventName == "canceled" || eventName == "aborted") { animation = "cancelled"; terminal = true; }
        else if (eventName == "error" || eventName == "failed") { animation = "error"; terminal = true; }
        else if (eventName == "idle" || eventName == "session_end") {
            sessions_.remove(sessionId);
            applySessionAnimation();
            return;
        }
        if (animation.isEmpty()) return;

        SessionState state;
        state.animation = animation;
        state.agentApp = normalizeAgentApp(event.value("agentApp").toString());
        state.model = event.value("model").toString();
        state.message = event.value("message").toString().left(180);
        state.updatedAt = QDateTime::currentDateTimeUtc();
        state.terminal = terminal;
        sessions_.insert(sessionId, state);
        applySessionAnimation();
        if (terminal) {
            QTimer::singleShot(5000, this, [this, sessionId, timestamp = state.updatedAt] {
                const auto it = sessions_.find(sessionId);
                if (it != sessions_.end() && it->updatedAt == timestamp) {
                    sessions_.remove(sessionId);
                    applySessionAnimation();
                }
            });
        }
    }

    void applySessionAnimation() {
        if (sessions_.isEmpty()) {
            setAllAnimations("idle");
            return;
        }
        auto latest = sessions_.cbegin();
        for (auto it = sessions_.cbegin(); it != sessions_.cend(); ++it) {
            if (it->updatedAt > latest->updatedAt)
                latest = it;
        }
        setAllAnimations(latest->animation);
        if (!latest->message.isEmpty()) {
            for (PetWindow *pet : visiblePets()) pet->setSpeech(latest->message);
        }
    }

    QJsonObject stateJson() const {
        QJsonArray pets;
        for (PetWindow *pet : visiblePets()) {
            pets.append(QJsonObject{{"id", pet->petId()}, {"name", pet->petName()},
                                    {"animation", pet->animation()}, {"speech", pet->speech()}});
        }
        QJsonArray sessions;
        for (auto it = sessions_.cbegin(); it != sessions_.cend(); ++it) {
            sessions.append(QJsonObject{{"sessionId", it.key()}, {"agentApp", it->agentApp},
                                        {"animation", it->animation}, {"model", it->model},
                                        {"updatedAt", it->updatedAt.toString(Qt::ISODate)}});
        }
        const QJsonArray supportedAgentApps = {"ZCode", "GitHub Copilot", "DeepSeek Harness",
                                               "OpenCode", "Antigravity", "Command Code"};
        return {{"app", "At-Pet"}, {"version", AT_PET_VERSION}, {"pets", pets},
                {"sessions", sessions}, {"supportedAgentApps", supportedAgentApps}};
    }

    void publishState() {
        if (api_) api_->publishState();
    }

    void updateLookDirection() {
#ifdef Q_OS_WIN
        CopilotWindowInfo info;
        EnumWindows(findCopilotWindow, reinterpret_cast<LPARAM>(&info));
        if (info.window) {
            for (PetWindow *pet : visiblePets()) {
                if (!pet->isVisible()) continue;
                const bool faceLeft = (info.bounds.left + info.bounds.right) / 2 < pet->frameGeometry().center().x();
                pet->setLookDirection(faceLeft);
            }
        }
#endif
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
        trayMenu_->addAction(tr("Summon selected pet"), this, [this] {
            for (PetWindow *pet : visiblePets()) pet->show();
            if (visiblePets().isEmpty()) startSelectedPet();
        });
        trayMenu_->addAction(tr("Summon all pets"), this, &PetController::summonAll);
        trayMenu_->addAction(tr("Import config + spritesheet…"), this, &PetController::importPet);
        trayMenu_->addAction(tr("Import pet folder…"), this, &PetController::importPetFolder);
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
        tray_ = new QSystemTrayIcon(makeTrayIcon(), this);
        rebuildTrayMenu();
        tray_->show();
    }

    HttpApi *api_ = nullptr;
    QSystemTrayIcon *tray_ = nullptr;
    QMenu *trayMenu_ = nullptr;
    QTranslator translator_;
    QString language_;
    bool translatorInstalled_ = false;
    QVector<QPointer<PetWindow>> pets_;
    QHash<QString, SessionState> sessions_;
    QTimer lookTimer_;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Ni-ShuWu"));
    QCoreApplication::setApplicationName(QStringLiteral("At-Pet"));
    QApplication::setQuitOnLastWindowClosed(false);
    PetController controller;
    return app.exec();
}

#include "main.moc"
