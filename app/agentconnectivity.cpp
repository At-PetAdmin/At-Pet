#include "agentconnectivity.h"

#include "agentregistry.h"

void AgentConnectivity::recordContact(const QString &agentApp, const QString &sessionId,
                                      const QDateTime &now) {
    if (agentApp.trimmed().isEmpty())
        return;
    expire(now);
    Connection &connection = connections_[agentAppKey(agentApp)];
    const bool wasConnected = !connection.sessions.isEmpty();
    connection.displayName = normalizeAgentApp(agentApp);
    connection.lastSeen = now;
    connection.status = QStringLiteral("connected");
    // /question permits contacts without a session ID.
    connection.sessions.insert(sessionId.trimmed(), now);
    emit changed();
    if (!wasConnected)
        emit agentConnected(connection.displayName);
}

void AgentConnectivity::recordEvent(const QJsonObject &event, const QDateTime &now) {
    const QString agentApp = event.value("agentApp").toString().trimmed();
    const QString sessionId = event.value("sessionId").toString().trimmed();
    const QString name = agentAppToken(event.value("event").toString());
    if (agentApp.isEmpty() || sessionId.isEmpty() || name.isEmpty())
        return;
    QString parentSessionId = event.value("parentSessionId").toString().trimmed();
    if (parentSessionId.isEmpty())
        parentSessionId = event.value("parent_session_id").toString().trimmed();
    // Subagent task completion must not leave a phantom connection after its
    // owning session ends, or disconnect the parent while it is still running.
    if (!parentSessionId.isEmpty()) {
        recordContact(agentApp, parentSessionId, now);
        return;
    }
    if (name != "sessionend" && name != "sessionended" &&
        name != "disconnected" && name != "disconnect") {
        recordContact(agentApp, sessionId, now);
        return;
    }

    expire(now);
    Connection &connection = connections_[agentAppKey(agentApp)];
    connection.displayName = normalizeAgentApp(agentApp);
    connection.lastSeen = now;
    connection.sessions.remove(sessionId);
    if (connection.sessions.isEmpty())
        connection.status = QStringLiteral("disconnected");
    emit changed();
}

void AgentConnectivity::expire(const QDateTime &now) {
    bool updated = false;
    const QDateTime cutoff = now.addSecs(-inactivityTimeoutSeconds);
    for (Connection &connection : connections_) {
        for (auto it = connection.sessions.begin(); it != connection.sessions.end();) {
            if (it.value() <= cutoff) {
                it = connection.sessions.erase(it);
                updated = true;
            } else {
                ++it;
            }
        }
        if (connection.status == "connected" && connection.sessions.isEmpty())
            connection.status = QStringLiteral("inactive");
    }
    if (updated)
        emit changed();
}

QJsonObject AgentConnectivity::snapshot() const {
    QJsonObject result;
    for (auto it = connections_.cbegin(); it != connections_.cend(); ++it) {
        result.insert(it.key(), QJsonObject{
            {"status", it->status}, {"connected", !it->sessions.isEmpty()},
            {"lastSeen", it->lastSeen.toUTC().toString(Qt::ISODateWithMs)},
            {"sessionCount", int(it->sessions.size())}});
    }
    return result;
}
