#pragma once

#include <QString>
#include <QUrl>

namespace agentsetup {
struct Result {
    bool ok = false;
    bool changed = false;
    QString path;
    QString backup;
    QString error;
};
bool canConfigure(const QString &key);
QString configPath(const QString &key, const QString &home);
QUrl guideUrl(const QString &key);
QString guideText(const QString &key);
// Explicit roots keep tests away from the user's real Agent configuration.
Result configure(const QString &key, const QString &home, const QString &runtimeRoot, quint16 port);
}
