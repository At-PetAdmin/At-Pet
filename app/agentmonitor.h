#pragma once

#include "agentregistry.h"

#include <QJsonObject>
#include <QString>

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
