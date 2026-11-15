#include "agentsetup.h"
#include "agentregistry.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("AgentSetup", text); }

struct Target { const char *key; const char *profile; const char *path; };
const Target targets[] = {
    {"claudecode", "claude-code", ".claude/settings.json"},
    {"openaicodex", "openai-codex", ".codex/hooks.json"},
    {"workbuddy", "workbuddy", ".codebuddy/settings.json"},
    {"commandcode", "command-code", ".commandcode/settings.json"},
    {"qwencode", "qwen-code", ".qwen/settings.json"},
};
const Target *targetFor(const QString &key) {
    for (const auto &target : targets) if (key == QLatin1String(target.key)) return &target;
    return nullptr;
}

bool save(const QString &path, const QByteArray &data, bool backup, agentsetup::Result *result) {
    QFile old(path);
    if (old.exists()) {
        if (!old.open(QIODevice::ReadOnly)) { result->error = tr("Could not read %1.").arg(path); return false; }
        if (old.readAll() == data) return true;
        old.close();
        if (backup) {
            const QString copy = path + ".at-pet-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz")
                + "-" + QUuid::createUuid().toString(QUuid::Id128).left(8) + ".bak";
            if (!QFile::copy(path, copy)) { result->error = tr("Could not back up %1.").arg(path); return false; }
            result->backup = copy;
        }
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) { result->error = tr("Could not create the configuration folder."); return false; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        result->error = tr("Could not save %1.").arg(path); return false;
    }
    result->changed = true;
    return true;
}
}

bool agentsetup::canConfigure(const QString &key) { return targetFor(key) != nullptr || key == "opencode"; }

QString agentsetup::configPath(const QString &key, const QString &home) {
    if (const auto *target = targetFor(key)) {
        // Respect Codex's explicitly selected configuration home.
        if (key == "openaicodex" && home == QDir::homePath() && !qEnvironmentVariable("CODEX_HOME").isEmpty())
            return QDir(qEnvironmentVariable("CODEX_HOME")).filePath("hooks.json");
        return QDir(home).filePath(QLatin1String(target->path));
    }
    if (key == "opencode") {
        const QString configHome = home == QDir::homePath() ? qEnvironmentVariable("XDG_CONFIG_HOME") : QString();
        return QDir(configHome.isEmpty() ? QDir(home).filePath(".config") : configHome).filePath("opencode/plugins/at-pet.ts");
    }
    return {};
}

QUrl agentsetup::guideUrl(const QString &key) {
    const QString base = "https://github.com/At-PetAdmin/";
    if (key == "openaicodex") return QUrl(base + "codex-at-pet#安装");
    if (key == "deepseekharness") return QUrl(base + "dsh_at-pet#安装");
    if (key == "githubcopilot") return QUrl(base + "github-copilot_at-pet#安装-a-copilot-app-扩展");
    if (key == "kimicode") return QUrl(base + "At-Pet/tree/beta/integrations/agent-plugins/kimi-code");
    const QHash<QString, QString> folders = {{"claudecode", "claude-code"}, {"opencode", "opencode"},
        {"workbuddy", "workbuddy"}, {"commandcode", "command-code"}, {"zcode", "zcode"},
        {"penguinharness", "penguin-harness"}};
    if (folders.contains(key)) return QUrl(base + "At-Pet_Agent-plugin/tree/main/Agent-plugin/" + folders.value(key));
    return QUrl(base + "At-Pet/blob/beta/docs/agent-compatibility.md");
}

QString agentsetup::guideText(const QString &key) {
    QString text;
    if (key == "openaicodex")
        text = tr("Install the organization's Codex plugin:\ncodex plugin marketplace add At-PetAdmin/codex-at-pet\ncodex plugin add codex-at-pet@at-pet-local\nRestart Codex, then review and trust hooks in /hooks or Settings > Hooks. Choose either the plugin or direct hooks to avoid duplicate events.");
    else if (key == "deepseekharness")
        text = tr("In the Harness Plugins page, install dsh-atpet. Or use:\nplugin_manager({ action: 'install_bundle', target: 'dsh-atpet' })\nFor a web CLI profile: dsh plugin --profile web add dsh-atpet\nDisable the old atpet-hooks-codex sender after verifying the plugin.");
    else if (key == "githubcopilot")
        text = tr("Clone At-PetAdmin/github-copilot_at-pet. From its root run:\nnode scripts/install.mjs app --repo <project>\nFor CLI hooks: node scripts/install.mjs cli --repo <project>\nReload the Copilot App extension or restart Copilot CLI.");
    else if (key == "opencode")
        text = tr("One-click setup installs at-pet.ts and client-auth.mjs into the global OpenCode plugins folder. Restart OpenCode V2 after setup. See the organization repository for manual installation.");
    else if (key == "kimicode")
        text = tr("Merge the Kimi Code config.toml hook example from the organization repository and point it to agent-bridge/bridge.mjs with the kimi-code profile. TOML is not changed automatically.");
    else if (canConfigure(key))
        text = tr("One-click setup merges At-Pet command hooks into the user configuration and backs up changed files. Node.js 20+ must be on the Agent's PATH. Restart the Agent and review its hooks. Plugin installation is an alternative; use only one sender.");
    else if (agentDefinitionFor(key) && agentDefinitionFor(key)->integration != AgentIntegration::ExternalRequired)
        text = tr("Follow this Agent's installation instructions in the organization repository. Its plugin or hook format requires manual setup. Restart the Agent after installation.");
    else text = tr("No sender is bundled for this Agent. Follow the compatibility guide to connect an external integration to the local API.");
    return text + "\n\n" + tr("At-Pet 1.3.4+ requires local API authentication. Update the sender too; Node integrations need client-auth.mjs beside the entry script. Older organization plugins may need an authentication update. A configured hook is only shown as Connected after a real event arrives.");
}

agentsetup::Result agentsetup::configure(const QString &key, const QString &home, const QString &runtimeRoot, quint16 port) {
    Result result;
    result.path = configPath(key, home);
    if (!canConfigure(key) || !port) { result.error = tr("This Agent requires manual setup."); return result; }
    if (key == "opencode") {
        QFile source(":/integrations/agent-plugins/opencode/at-pet.ts");
        QFile auth(":/integrations/agent-plugins/opencode/client-auth.mjs");
        if (!source.open(QIODevice::ReadOnly) || !auth.open(QIODevice::ReadOnly)) { result.error = tr("Bundled integration files are missing."); return result; }
        QByteArray plugin = source.readAll();
        plugin.replace("http://127.0.0.1:8765", QString("http://127.0.0.1:%1").arg(port).toUtf8());
        if (!save(QFileInfo(result.path).dir().filePath("client-auth.mjs"), auth.readAll(), true, &result)) return result;
        if (!save(result.path, plugin, true, &result)) return result;
        result.ok = true; return result;
    }
    // Parse and validate before touching the existing configuration or runtime.
    QJsonObject config;
    QFile input(result.path);
    if (input.exists()) {
        if (!input.open(QIODevice::ReadOnly)) { result.error = tr("Could not read %1.").arg(result.path); return result; }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(input.readAll(), &error);
        input.close(); // Windows must release the original before QSaveFile replaces it.
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            result.error = tr("Invalid JSON; the existing configuration was left unchanged."); return result;
        }
        config = doc.object();
    }
    if (config.contains("hooks") && !config.value("hooks").isObject()) {
        result.error = tr("Invalid hooks format; the existing configuration was left unchanged."); return result;
    }
    auto hooks = config.value("hooks").toObject();
    const auto *target = targetFor(key);
    QStringList events = {"SessionStart", "UserPromptSubmit", "PreToolUse", "PostToolUse", "Stop", "SessionEnd"};
    if (key == "commandcode") events = {"SessionStart", "PreToolUse", "PostToolUse", "Stop"};
    if (key == "claudecode" || key == "openaicodex" || key == "qwencode") events << "PermissionRequest";
    for (const QString &event : events) {
        if (hooks.contains(event) && !hooks.value(event).isArray()) {
            result.error = tr("Invalid hooks format; the existing configuration was left unchanged."); return result;
        }
    }
    const QString root = QDir(runtimeRoot).filePath("agent-bridge");
    QDirIterator files(":/integrations/agent-bridge", QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString path = files.next();
        QFile source(path);
        if (!source.open(QIODevice::ReadOnly)) { result.error = tr("Bundled integration files are missing."); return result; }
        if (!save(QDir(root).filePath(path.mid(QString(":/integrations/agent-bridge/").size())), source.readAll(), false, &result)) return result;
    }
    const QString launcher = QDir::fromNativeSeparators(QDir(root).filePath(QString("at-pet-%1.mjs").arg(target->profile)));
    const QByteArray script = "import { main } from './bridge.mjs';\nmain(['--profile', '" + QByteArray(target->profile)
        + "', '--url', 'http://127.0.0.1:" + QByteArray::number(port) + "']).catch(() => {});\n";
    if (!save(launcher, script, false, &result)) return result;
    // JSON encoding handles configuration escaping; reject shell expansions in a path.
    if (launcher.contains(QRegularExpression(QStringLiteral("[\\\"$`%\\r\\n]")))) {
        result.error = tr("The integration path contains unsupported shell characters."); return result;
    }
    const QString command = QString("node \"%1\"").arg(launcher);
    for (const QString &event : events) {
        auto entries = hooks.value(event).toArray();
        const QJsonObject handler{{"type", "command"}, {"command", command}, {"timeout", event == "PermissionRequest" ? 600 : 10}};
        const QJsonObject entry{{"hooks", QJsonArray{handler}}};
        // Only our exact entry is deduplicated; other hooks and matchers are preserved.
        if (!entries.contains(entry)) entries.append(entry);
        hooks.insert(event, entries);
    }
    config.insert("hooks", hooks);
    if (!save(result.path, QJsonDocument(config).toJson(QJsonDocument::Indented), true, &result)) return result;
    result.ok = true;
    return result;
}
