#pragma once

#include "agentregistry.h"

#include <QJsonObject>
#include <QString>

inline void copyAgentEventMetadata(const QJsonObject &source, QJsonObject *event) {
    for (const auto &key : {"eventId", "runId", "sequence", "occurredAt", "toolCallId", "turnStart"})
        if (source.contains(key)) event->insert(key, source.value(key));
    if (!event->contains("toolCallId") && source.contains("tool_use_id"))
        event->insert("toolCallId", source.value("tool_use_id"));
    if (!event->contains("runId") && source.contains("prompt_id"))
        event->insert("runId", source.value("prompt_id"));
}

class AgentMonitorStrategy {
public:
    virtual ~AgentMonitorStrategy() = default;

    virtual bool acceptsNativeEvents() const { return false; }
    virtual bool usesNativeHookResponse() const { return false; }
    virtual bool normalizeEvent(const QJsonObject &source, QJsonObject *event,
                                QString *error) const = 0;
    virtual bool isPermissionRequest(const QJsonObject &) const { return false; }
    virtual bool normalizePermissionRequest(const QJsonObject &, QJsonObject *,
                                           QString *) const { return false; }
    virtual QJsonObject permissionResponse(bool) const { return {}; }
};

const AgentMonitorStrategy *agentMonitorStrategy(AgentMonitorType type);
