#pragma once

#include <QStringList>

namespace crashreporter {
// Call before constructing QApplication so platform-plugin failures are recorded too.
bool start(const QString &directory = {});
void stop();
QString logDirectory();
QStringList pendingReports();
bool exportReport(const QString &report, const QString &destination);
void acknowledgeReport(const QString &report);
}
