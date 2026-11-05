#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>

// Hook senders use short-lived HTTP requests, so connectivity is inferred from
// recent traffic, independently of task animations and event filters.
class AgentConnectivity final : public QObject {
    Q_OBJECT

public:
    static constexpr int inactivityTimeoutSeconds = 1800;

    explicit AgentConnectivity(QObject *parent = nullptr) : QObject(parent) {}
    void recordEvent(const QJsonObject &event,
                     const QDateTime &now = QDateTime::currentDateTimeUtc());
    void recordContact(const QString &agentApp, const QString &sessionId,
                       const QDateTime &now = QDateTime::currentDateTimeUtc());
    void expire(const QDateTime &now = QDateTime::currentDateTimeUtc());
    QJsonObject snapshot() const;

signals:
    void changed();
    void agentConnected(const QString &displayName);

private:
    struct Connection {
        QString displayName;
        QString status;
        QDateTime lastSeen;
        QHash<QString, QDateTime> sessions;
    };
    QHash<QString, Connection> connections_;
};
