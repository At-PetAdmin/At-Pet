#pragma once

#include <QList>
#include <QString>

struct ApiClient {
    QString id;
    QString agentKey;
    QByteArray token;
    bool administrator = false;
};

namespace apicredentials {
QString defaultPath();
// Fail closed on unreadable/invalid credentials; never replace a damaged credential store.
bool loadOrCreate(QList<ApiClient> *clients, QString *error, const QString &path = defaultPath());
}
