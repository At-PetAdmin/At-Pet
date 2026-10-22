#include "adapter.h"

namespace agentadapters {
namespace {
class OpenCodeV2PluginStrategy final : public AgentMonitorStrategy {
public:
    bool acceptsNativeEvents() const override { return true; }

    bool normalizeEvent(const QJsonObject &source, QJsonObject *event,
                        QString *error) const override {
        if (!event || !error)
            return false;
        *event = QJsonObject();
        error->clear();

        const QString sourceEvent = source.value("sourceEvent").toString().trimmed();
        const QString sessionId = source.value("sessionID").toString().trimmed();
        if (sourceEvent.isEmpty() || sessionId.isEmpty()) {
            *error = QStringLiteral("sourceEvent and sessionID are required");
            return false;
        }

        QString normalizedName;
        if (sourceEvent == "session.created")
            normalizedName = QStringLiteral("started");
        else if (sourceEvent == "session.status") {
            const QString status = source.value("statusType").toString().trimmed();
            if (status == "idle")
                normalizedName = QStringLiteral("idle");
            else if (status == "busy" || status == "retry")
                normalizedName = QStringLiteral("thinking");
            else
                return true;
        } else if (sourceEvent == "session.idle" || sourceEvent == "session.deleted")
            normalizedName = QStringLiteral("idle");
        else if (sourceEvent == "tool.execute.before")
            normalizedName = QStringLiteral("tool_start");
        else if (sourceEvent == "tool.execute.after")
            normalizedName = source.value("statusType").toString() == "error"
                ? QStringLiteral("error") : QStringLiteral("tool_end");
        else
            return true;

        *event = {{"sessionId", sessionId.left(256)},
                  {"agentApp", "opencode"},
                  {"event", normalizedName}};
        QString parentSessionId = source.value("parentID").toString().trimmed();
        if (parentSessionId.isEmpty())
            parentSessionId = source.value("parentSessionID").toString().trimmed();
        if (!parentSessionId.isEmpty()) {
            event->insert("parentSessionId", parentSessionId.left(256));
            event->insert("isSubagent", true);
        }
        QString toolName = source.value("toolName").toString().trimmed();
        if (toolName.isEmpty())
            toolName = source.value("tool").toString().trimmed();
        if (!toolName.isEmpty()) {
            event->insert("toolName", toolName.left(160));
            if (sourceEvent == "tool.execute.before")
                event->insert("message", toolName.left(180));
        }
        QString message = source.value("message").toString().trimmed();
        if (message.isEmpty())
            message = source.value("statusMessage").toString().trimmed();
        if (!message.isEmpty())
            event->insert("message", message.left(180));
        return true;
    }
};
}

const AgentMonitorStrategy *openCodeV2PluginStrategy() {
    static const OpenCodeV2PluginStrategy strategy;
    return &strategy;
}
}
