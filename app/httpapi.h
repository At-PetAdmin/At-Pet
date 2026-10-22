#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <functional>

class HttpApi final : public QObject {
    Q_OBJECT

public:
    explicit HttpApi(std::function<QJsonObject()> stateProvider, QObject *parent = nullptr);
    bool start(quint16 port = 8765);
    quint16 port() const;
    void publishState();
    void resolvePermission(const QString &requestId, bool allowOnce);

signals:
    void eventReceived(const QJsonObject &event);
    void permissionRequestReceived(const QString &requestId, const QJsonObject &request);
    void permissionRequestCancelled(const QString &requestId);
    void animationRequested(const QString &name);
    void speechRequested(const QString &text);
    void summonRequested(const QJsonObject &request);

private slots:
    void acceptConnections();

private:
    void readRequest(QTcpSocket *socket);
    void routeRequest(QTcpSocket *socket, const QByteArray &method, const QByteArray &path,
                      const QByteArray &body);
    void sendJson(QTcpSocket *socket, int status, const QJsonObject &json);
    void broadcastState();

    struct PermissionRequest {
        QJsonObject details;
        QString decision = QStringLiteral("pending");
        bool isClaudeHttpHook = false;
        QPointer<QTcpSocket> hookSocket;
    };

    QTcpServer server_;
    std::function<QJsonObject()> stateProvider_;
    QList<QTcpSocket *> eventClients_;
    QHash<QTcpSocket *, QByteArray> requestBuffers_;
    QHash<QString, PermissionRequest> permissionRequests_;
    QHash<QTcpSocket *, QString> permissionHookRequests_;
};
