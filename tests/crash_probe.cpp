#include "crashreporter.h"

#include <QCoreApplication>
#include <QDebug>
#include <csignal>
#include <exception>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() != 3 || !crashreporter::start(arguments.at(1))) return 2;
    qWarning("crash probe warning");
    const QString mode = arguments.at(2);
    if (mode == "fatal") qFatal("crash probe fatal");
    if (mode == "abort") std::raise(SIGABRT);
    if (mode == "terminate") std::terminate();
#ifdef Q_OS_WIN
    if (mode == "seh") {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    }
#endif
    if (mode == "limit") {
        const QString message(8192, QLatin1Char('x'));
        for (int i = 0; i < 200; ++i) qWarning().noquote() << message;
        qFatal("fatal after log limit");
    }
    crashreporter::stop();
    return 0;
}
