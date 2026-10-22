#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

enum class AgentMonitorType {
    ExternalHttpApi,
    ClaudeCodeHttpHooks,
    OpenCodeV2Plugin
};

struct AgentDefinition {
    QString key;
    QString displayName;
    QString description;
    QStringList aliases;
    AgentMonitorType monitorType = AgentMonitorType::ExternalHttpApi;
};

inline const QVector<AgentDefinition> &agentDefinitions() {
    static const QVector<AgentDefinition> definitions = {
        {QStringLiteral("githubcopilot"), QStringLiteral("GitHub Copilot"),
         QStringLiteral("VS Code / Visual Studio / JetBrains Copilot Chat"),
         {QStringLiteral("copilot"), QStringLiteral("githubcopilotchat"),
          QStringLiteral("vscodecopilot"), QStringLiteral("visualstudiocopilot"),
          QStringLiteral("jetbrainscopilot")}, AgentMonitorType::ExternalHttpApi},
        {QStringLiteral("zcode"), QStringLiteral("ZCode"),
         QStringLiteral("ZCode Agent and development assistant"), {QStringLiteral("zcodeassistant")},
         AgentMonitorType::ExternalHttpApi},
        {QStringLiteral("deepseekharness"), QStringLiteral("DeepSeek Harness"),
         QStringLiteral("DeepSeek CLI and local agent workflow harness"),
         {QStringLiteral("deepseek"), QStringLiteral("deepseekcli")}, AgentMonitorType::ExternalHttpApi},
        {QStringLiteral("opencode"), QStringLiteral("OpenCode"),
         QStringLiteral("Open-source terminal AI coding assistant"), {QStringLiteral("opencodeai")},
         AgentMonitorType::OpenCodeV2Plugin},
        {QStringLiteral("antigravity"), QStringLiteral("Antigravity"),
         QStringLiteral("Antigravity Agent tools and IDE extensions"), {}, AgentMonitorType::ExternalHttpApi},
        {QStringLiteral("commandcode"), QStringLiteral("Command Code"),
         QStringLiteral("CLI command automation agent"), {QStringLiteral("command")},
         AgentMonitorType::ExternalHttpApi},
        {QStringLiteral("claudecode"), QStringLiteral("Claude Code"),
         QStringLiteral("Anthropic Claude Code CLI and hooks"),
         {QStringLiteral("claude"), QStringLiteral("claudecli")}, AgentMonitorType::ClaudeCodeHttpHooks},
        {QStringLiteral("openaicodex"), QStringLiteral("OpenAI Codex"),
         QStringLiteral("OpenAI Codex CLI and agent events"),
         {QStringLiteral("codex"), QStringLiteral("codexcli"), QStringLiteral("openaicli")},
         AgentMonitorType::ExternalHttpApi},
        {QStringLiteral("geminicli"), QStringLiteral("Gemini CLI"),
         QStringLiteral("Google Gemini CLI and extensions"),
         {QStringLiteral("gemini"), QStringLiteral("googlegemini")}, AgentMonitorType::ExternalHttpApi},
        {QStringLiteral("cursor"), QStringLiteral("Cursor"),
         QStringLiteral("Cursor editor and agent events"), {QStringLiteral("cursorai")},
         AgentMonitorType::ExternalHttpApi},
        {QStringLiteral("windsurf"), QStringLiteral("Windsurf"),
         QStringLiteral("Windsurf editor and agent events"), {QStringLiteral("codeium")},
         AgentMonitorType::ExternalHttpApi}
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
