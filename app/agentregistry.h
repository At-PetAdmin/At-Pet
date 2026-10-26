#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

enum class AgentMonitorType {
    ExternalHttpApi,
    ClaudeCodeHttpHooks,
    OpenCodeV2Plugin
};

// What actually ships in this repository for a given agent. The settings window
// uses it so a listed agent is never mistaken for a working integration.
enum class AgentIntegration {
    BuiltInAdapter,   // native C++ adapter, nothing to configure
    ProfileShipped,   // bridge/profile included, the agent still needs its hook configured
    ExternalRequired  // name only; this repository ships no sender yet
};

struct AgentDefinition {
    QString key;
    QString displayName;
    QString description;
    QStringList aliases;
    AgentMonitorType monitorType = AgentMonitorType::ExternalHttpApi;
    AgentIntegration integration = AgentIntegration::ExternalRequired;
};

inline const QVector<AgentDefinition> &agentDefinitions() {
    static const QVector<AgentDefinition> definitions = {
        {QStringLiteral("githubcopilot"), QStringLiteral("GitHub Copilot"),
         QStringLiteral("GitHub Copilot CLI; the bridge profile is included, the Copilot app exposes no status API"),
         {QStringLiteral("copilot"), QStringLiteral("githubcopilotchat"),
          QStringLiteral("vscodecopilot"), QStringLiteral("visualstudiocopilot"),
          QStringLiteral("jetbrainscopilot")}, AgentMonitorType::ExternalHttpApi,
         AgentIntegration::ProfileShipped},
        {QStringLiteral("zcode"), QStringLiteral("ZCode"),
         QStringLiteral("ZCode CLI hooks"), {QStringLiteral("zcodeassistant")},
         AgentMonitorType::ExternalHttpApi, AgentIntegration::ProfileShipped},
        {QStringLiteral("kimicode"), QStringLiteral("Kimi Code"),
         QStringLiteral("Kimi Code CLI config.toml hooks"),
         {QStringLiteral("kimi"), QStringLiteral("kimicli"), QStringLiteral("kimicodecli")},
         AgentMonitorType::ExternalHttpApi, AgentIntegration::ProfileShipped},
        {QStringLiteral("deepseekharness"), QStringLiteral("DeepSeek Harness"),
         QStringLiteral("DeepSeek Harness hook bridge"),
         {QStringLiteral("deepseek"), QStringLiteral("deepseekcli")},
         AgentMonitorType::ExternalHttpApi, AgentIntegration::ProfileShipped},
        {QStringLiteral("opencode"), QStringLiteral("OpenCode"),
         QStringLiteral("Open-source terminal AI coding assistant"), {QStringLiteral("opencodeai")},
         AgentMonitorType::OpenCodeV2Plugin, AgentIntegration::BuiltInAdapter},
        {QStringLiteral("antigravity"), QStringLiteral("Antigravity"),
         QStringLiteral("Antigravity agent tools; no sender is bundled yet"), {},
         AgentMonitorType::ExternalHttpApi, AgentIntegration::ExternalRequired},
        {QStringLiteral("commandcode"), QStringLiteral("Command Code"),
         QStringLiteral("Command Code CLI hooks"), {QStringLiteral("command")},
         AgentMonitorType::ExternalHttpApi, AgentIntegration::ProfileShipped},
        {QStringLiteral("claudecode"), QStringLiteral("Claude Code"),
         QStringLiteral("Anthropic Claude Code CLI HTTP hooks"),
         {QStringLiteral("claude"), QStringLiteral("claudecli")},
         AgentMonitorType::ClaudeCodeHttpHooks, AgentIntegration::BuiltInAdapter},
        {QStringLiteral("openaicodex"), QStringLiteral("OpenAI Codex"),
         QStringLiteral("OpenAI Codex CLI hooks and permission requests"),
         {QStringLiteral("codex"), QStringLiteral("codexcli"), QStringLiteral("openaicli")},
         AgentMonitorType::ExternalHttpApi, AgentIntegration::ProfileShipped},
        {QStringLiteral("geminicli"), QStringLiteral("Gemini CLI"),
         QStringLiteral("Google Gemini CLI hooks; no sender is bundled yet"),
         {QStringLiteral("gemini"), QStringLiteral("googlegemini")},
         AgentMonitorType::ExternalHttpApi, AgentIntegration::ExternalRequired},
        {QStringLiteral("cursor"), QStringLiteral("Cursor"),
         QStringLiteral("Cursor agent hooks; no sender is bundled yet"), {QStringLiteral("cursorai")},
         AgentMonitorType::ExternalHttpApi, AgentIntegration::ExternalRequired},
        {QStringLiteral("windsurf"), QStringLiteral("Windsurf"),
         QStringLiteral("Windsurf agent hooks; no sender is bundled yet"), {QStringLiteral("codeium")},
         AgentMonitorType::ExternalHttpApi, AgentIntegration::ExternalRequired},
        {QStringLiteral("workbuddy"), QStringLiteral("WorkBuddy"),
         QStringLiteral("Tencent WorkBuddy with CodeBuddy-compatible hooks"), {QStringLiteral("codebuddy")},
         AgentMonitorType::ExternalHttpApi, AgentIntegration::ProfileShipped},
        {QStringLiteral("penguinharness"), QStringLiteral("PenguinHarness"),
         QStringLiteral("PenguinHarness plugin hooks"), {QStringLiteral("penguin")},
         AgentMonitorType::ExternalHttpApi, AgentIntegration::ProfileShipped},
        {QStringLiteral("trae"), QStringLiteral("Trae"),
         QStringLiteral("Trae IDE and Trae SOLO hooks; observation only, approvals stay in Trae"),
         {QStringLiteral("traeai"), QStringLiteral("traesolo")},
         AgentMonitorType::ExternalHttpApi, AgentIntegration::ProfileShipped},
        {QStringLiteral("qoder"), QStringLiteral("Qoder"),
         QStringLiteral("Qoder IDE, JetBrains plugin and CLI hooks"), {QStringLiteral("qoderai")},
         AgentMonitorType::ExternalHttpApi, AgentIntegration::ProfileShipped},
        {QStringLiteral("qodercn"), QStringLiteral("Qoder CN"),
         QStringLiteral("Qoder CN, formerly Tongyi Lingma"),
         {QStringLiteral("lingma"), QStringLiteral("tongyilingma")},
         AgentMonitorType::ExternalHttpApi, AgentIntegration::ProfileShipped},
        {QStringLiteral("qwencode"), QStringLiteral("Qwen Code"),
         QStringLiteral("Alibaba Qwen Code CLI hooks"),
         {QStringLiteral("qwen"), QStringLiteral("qwencli"), QStringLiteral("qwencodecli")},
         AgentMonitorType::ExternalHttpApi, AgentIntegration::ProfileShipped},
    };
    return definitions;
}

inline QString agentAppToken(const QString &name) {
    QString token;
    for (const QChar character : name.trimmed().toLower()) {
        if (character.isLetterOrNumber())
            token.append(character);
    }
    return token;
}

inline QString agentAppKey(const QString &name) {
    const QString token = agentAppToken(name);
    if (token.isEmpty())
        return QStringLiteral("external");
    for (const AgentDefinition &agent : agentDefinitions()) {
        if (token == agent.key || agent.aliases.contains(token))
            return agent.key;
    }
    return token;
}

inline QString agentMonitorTypeId(AgentMonitorType type) {
    switch (type) {
    case AgentMonitorType::ExternalHttpApi:
        return QStringLiteral("external_http_api");
    case AgentMonitorType::ClaudeCodeHttpHooks:
        return QStringLiteral("claude_code_http_hooks");
    case AgentMonitorType::OpenCodeV2Plugin:
        return QStringLiteral("opencode_v2_plugin");
    }
    return QStringLiteral("external_http_api");
}

inline QString agentIntegrationId(AgentIntegration integration) {
    switch (integration) {
    case AgentIntegration::BuiltInAdapter:
        return QStringLiteral("built_in_adapter");
    case AgentIntegration::ProfileShipped:
        return QStringLiteral("bridge_profile");
    case AgentIntegration::ExternalRequired:
        return QStringLiteral("external_required");
    }
    return QStringLiteral("external_required");
}

inline const AgentDefinition *agentDefinitionFor(const QString &name) {
    const QString key = agentAppKey(name);
    for (const AgentDefinition &agent : agentDefinitions()) {
        if (agent.key == key)
            return &agent;
    }
    return nullptr;
}

inline QString normalizeAgentApp(const QString &name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return QStringLiteral("external");
    const QString key = agentAppKey(trimmed);
    for (const AgentDefinition &agent : agentDefinitions()) {
        if (key == agent.key)
            return agent.displayName;
    }
    return trimmed.left(64);
}
