#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
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

signals:
    void eventReceived(const QJsonObject &event);
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

    QTcpServer server_;
    std::function<QJsonObject()> stateProvider_;
    QList<QTcpSocket *> eventClients_;
    QHash<QTcpSocket *, QByteArray> requestBuffers_;
};
