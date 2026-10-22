#include "adapter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

namespace agentadapters {
namespace {
QString compactValue(const QJsonValue &value) {
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    if (value.isString())
        return value.toString();
    return {};
}

class ClaudeCodeHttpHooksStrategy final : public AgentMonitorStrategy {
public:
    bool acceptsNativeEvents() const override { return true; }
    bool usesNativeHookResponse() const override { return true; }

    bool normalizeEvent(const QJsonObject &source, QJsonObject *event,
                        QString *error) const override {
        if (!event || !error)
            return false;
        *event = QJsonObject();
        error->clear();

        const QString sessionId = source.value("session_id").toString().trimmed();
        const QString hookName = source.value("hook_event_name").toString().trimmed();
        if (sessionId.isEmpty() || hookName.isEmpty()) {
            *error = QStringLiteral("session_id and hook_event_name are required");
            return false;
        }

        QString normalizedName;
        if (hookName == "UserPromptSubmit")
            normalizedName = QStringLiteral("thinking");
        else if (hookName == "PreToolUse")
            normalizedName = QStringLiteral("tool_start");
        else if (hookName == "PostToolUse")
            normalizedName = QStringLiteral("tool_end");
        else if (hookName == "PostToolUseFailure")
            normalizedName = QStringLiteral("error");
        else if (hookName == "Stop" || hookName == "SubagentStop")
            normalizedName = QStringLiteral("completed");
        else if (hookName == "SessionEnd")
            normalizedName = QStringLiteral("idle");
        else if (hookName == "SubagentStart")
            normalizedName = QStringLiteral("thinking");
        else
            return true;

        QString normalizedSessionId = sessionId;
        const QString agentId = source.value("agent_id").toString().trimmed();
        const bool isSubagent = hookName.startsWith("Subagent") && !agentId.isEmpty();
        if (isSubagent)
            normalizedSessionId += QLatin1Char(':') + agentId;

        *event = {{"sessionId", normalizedSessionId},
                  {"agentApp", "claude-code"},
                  {"event", normalizedName}};
        if (isSubagent) {
            event->insert("isSubagent", true);
            event->insert("parentSessionId", sessionId);
        }
        const QString toolName = source.value("tool_name").toString().trimmed();
        if (!toolName.isEmpty()) {
            event->insert("toolName", toolName.left(160));
            if (hookName == "PreToolUse")
                event->insert("message", toolName.left(180));
        }
        if (hookName == "PostToolUseFailure") {
            const QString message = compactValue(source.value("error"));
            if (!message.isEmpty())
                event->insert("message", message.left(180));
        }
        return true;
    }

    bool isPermissionRequest(const QJsonObject &source) const override {
        return source.value("hook_event_name").toString() == "PermissionRequest";
    }

    bool normalizePermissionRequest(const QJsonObject &source, QJsonObject *request,
                                   QString *error) const override {
        if (!request || !error)
            return false;
        *request = QJsonObject();
        error->clear();

        const QString sessionId = source.value("session_id").toString().trimmed();
        const QString toolName = source.value("tool_name").toString().trimmed();
        if (sessionId.isEmpty() || toolName.isEmpty()) {
            *error = QStringLiteral("session_id and tool_name are required for PermissionRequest");
            return false;
        }

        *request = {{"sessionId", sessionId.left(256)},
                    {"agentApp", "claude-code"},
                    {"toolName", toolName.left(160)},
                    {"title", QStringLiteral("Claude Code permission request")},
                    {"description", compactValue(source.value("tool_input")).left(2000)}};
        return true;
    }

    QJsonObject permissionResponse(bool allowOnce) const override {
        QJsonObject decision;
        decision.insert(QStringLiteral("behavior"), allowOnce ? QStringLiteral("allow") : QStringLiteral("deny"));

        QJsonObject hookOutput;
        hookOutput.insert(QStringLiteral("hookEventName"), QStringLiteral("PermissionRequest"));
        hookOutput.insert(QStringLiteral("decision"), decision);

        QJsonObject response;
        response.insert(QStringLiteral("hookSpecificOutput"), hookOutput);
        return response;
    }
};
}

const AgentMonitorStrategy *claudeCodeHttpHooksStrategy() {
    static const ClaudeCodeHttpHooksStrategy strategy;
    return &strategy;
}
}
