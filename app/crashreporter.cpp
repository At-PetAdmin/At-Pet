#include "crashreporter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUuid>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <exception>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
constexpr unsigned maxLogBytes = 1024 * 1024;
QString directoryPath;
QString currentLog;
QtMessageHandler previousMessageHandler = nullptr;
std::terminate_handler previousTerminateHandler = nullptr;
using SignalHandler = void (*)(int);
SignalHandler previousAbortHandler = SIG_DFL;
std::atomic<quint64> logBytes{0};
std::atomic_flag crashRecorded = ATOMIC_FLAG_INIT;

#ifdef Q_OS_WIN
using FileHandle = HANDLE;
const FileHandle invalidFile = INVALID_HANDLE_VALUE;
LPTOP_LEVEL_EXCEPTION_FILTER previousExceptionFilter = nullptr;
quintptr executableBase = 0;
FileHandle openFile(const QString &path) {
    return CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_WRITE,
                       FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
}
void writeBytes(FileHandle file, const char *data, unsigned size) {
    DWORD written = 0;
    if (file != invalidFile) WriteFile(file, data, size, &written, nullptr);
}
void flushFile(FileHandle file) {
    if (file != invalidFile) FlushFileBuffers(file);
}
void closeFile(FileHandle file) {
    if (file != invalidFile) CloseHandle(file);
}
#else
using FileHandle = int;
constexpr FileHandle invalidFile = -1;
FileHandle openFile(const QString &path) {
    return ::open(QFile::encodeName(path).constData(), O_WRONLY | O_CREAT | O_EXCL | O_APPEND, 0600);
}
void writeBytes(FileHandle file, const char *data, unsigned size) {
    if (file != invalidFile) (void)::write(file, data, size);
}
void flushFile(FileHandle file) {
    if (file != invalidFile) (void)::fsync(file);
}
void closeFile(FileHandle file) {
    if (file != invalidFile) ::close(file);
}
#endif
FileHandle logFile = invalidFile;
FileHandle crashFile = invalidFile;

// Crash handlers use only fixed buffers and pre-opened files: no Qt, heap allocation,
// dialogs or application locks in a potentially corrupted process.
void appendText(const char *text) {
    unsigned size = 0;
    while (size < 512 && text[size]) ++size;
    writeBytes(crashFile, text, size);
}
void appendHex(const char *label, quintptr value) {
    appendText(label);
    char digits[2 * sizeof(value) + 3] = {'0', 'x'};
    constexpr unsigned count = 2 * sizeof(value);
    for (unsigned i = 0; i < count; ++i)
        digits[2 + i] = "0123456789ABCDEF"[(value >> ((count - i - 1) * 4)) & 0xf];
    digits[count + 2] = '\n';
    writeBytes(crashFile, digits, sizeof(digits));
}
bool beginCrash(const char *reason) {
    if (crashRecorded.test_and_set()) return false;
    appendText("\nAt-Pet crash report\nReason: ");
    appendText(reason);
    appendText("\n");
    return true;
}
void finishCrash() {
    flushFile(crashFile);
    flushFile(logFile);
}
void abortHandler(int) {
    if (beginCrash("SIGABRT (abort)")) finishCrash();
    std::_Exit(EXIT_FAILURE);
}
void terminateHandler() {
    if (beginCrash("std::terminate (unhandled C++ exception or termination)")) finishCrash();
    std::_Exit(EXIT_FAILURE);
}
#ifdef Q_OS_WIN
LONG WINAPI exceptionFilter(EXCEPTION_POINTERS *exception) {
    if (beginCrash("Unhandled Windows exception")) {
        const EXCEPTION_RECORD *record = exception->ExceptionRecord;
        appendHex("Exception code: ", record->ExceptionCode);
        appendHex("Exception address: ", reinterpret_cast<quintptr>(record->ExceptionAddress));
        appendHex("Executable base: ", executableBase);
        appendHex("Thread ID: ", GetCurrentThreadId());
        for (DWORD i = 0; i < record->NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS; ++i)
            appendHex("Exception parameter: ", record->ExceptionInformation[i]);
#if defined(_M_X64) || defined(__x86_64__)
        appendHex("Instruction pointer: ", exception->ContextRecord->Rip);
        appendHex("Stack pointer: ", exception->ContextRecord->Rsp);
#elif defined(_M_ARM64) || defined(__aarch64__)
        appendHex("Instruction pointer: ", exception->ContextRecord->Pc);
        appendHex("Stack pointer: ", exception->ContextRecord->Sp);
#endif
        finishCrash();
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    const QByteArray line = (QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
        + QLatin1Char(' ') + qFormatLogMessage(type, context, message).left(8192)
        + QLatin1Char('\n')).toUtf8();
    const unsigned size = unsigned(line.size());
    const quint64 offset = logBytes.fetch_add(size);
    if (offset < maxLogBytes) {
        writeBytes(logFile, line.constData(), qMin(size, maxLogBytes - unsigned(offset)));
    }
    if (type == QtFatalMsg) {
        if (beginCrash("Qt fatal message (see session log)")) {
            // Preserve the fatal message even after the normal log limit was reached.
            writeBytes(crashFile, line.constData(), size);
            finishCrash();
        }
    }
    if (previousMessageHandler) previousMessageHandler(type, context, message);
}
}

namespace crashreporter {
bool start(const QString &directory) {
    if (logFile != invalidFile) return true;
    directoryPath = directory.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
              .filePath(QStringLiteral("logs"))
        : directory;
    if (!QDir().mkpath(directoryPath)) return false;
    const QString name = QStringLiteral("at-pet-%1-%2-%3.log")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")))
        .arg(QCoreApplication::applicationPid())
        .arg(QUuid::createUuid().toString(QUuid::Id128));
    currentLog = QDir(directoryPath).filePath(name);
    logFile = openFile(currentLog);
    crashFile = openFile(currentLog + QStringLiteral(".crash"));
    if (logFile == invalidFile || crashFile == invalidFile) {
        closeFile(logFile);
        closeFile(crashFile);
        // Only remove files we successfully created (never someone else's file).
        if (logFile != invalidFile) QFile::remove(currentLog);
        if (crashFile != invalidFile) QFile::remove(currentLog + QStringLiteral(".crash"));
        logFile = crashFile = invalidFile;
        return false;
    }
    crashRecorded.clear();
    logBytes.store(0);
    const QByteArray header = QStringLiteral(
        "At-Pet %1\nStarted (UTC): %2\nOS: %3\nArchitecture: %4\nQt: %5\n\n")
        .arg(QStringLiteral(AT_PET_VERSION), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
             QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(), QString::fromLatin1(qVersion()))
        .toUtf8();
    writeBytes(logFile, header.constData(), unsigned(header.size()));
    previousMessageHandler = qInstallMessageHandler(messageHandler);
    previousTerminateHandler = std::set_terminate(terminateHandler);
    previousAbortHandler = std::signal(SIGABRT, abortHandler);
#ifdef Q_OS_WIN
    executableBase = reinterpret_cast<quintptr>(GetModuleHandleW(nullptr));
    previousExceptionFilter = SetUnhandledExceptionFilter(exceptionFilter);
#endif
    // Retain the latest ten ordinary sessions; unexported crash reports are never pruned.
    const auto logs = QDir(directoryPath).entryInfoList({QStringLiteral("at-pet-*.log")}, QDir::Files, QDir::Name);
    int ordinary = 0;
    for (auto it = logs.crbegin(); it != logs.crend(); ++it) {
        const QString marker = it->absoluteFilePath() + QStringLiteral(".crash");
        // Existing markers include other running processes, even when still empty.
        if (QFile::exists(marker)) continue;
        if (++ordinary > 10) QFile::remove(it->absoluteFilePath());
    }
    return true;
}

void stop() {
    if (logFile == invalidFile) return;
    qInstallMessageHandler(previousMessageHandler);
    std::set_terminate(previousTerminateHandler);
    std::signal(SIGABRT, previousAbortHandler);
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(previousExceptionFilter);
#endif
    writeBytes(logFile, "\nClean shutdown\n", 16);
    closeFile(logFile);
    closeFile(crashFile);
    logFile = crashFile = invalidFile;
    const QString marker = currentLog + QStringLiteral(".crash");
    if (QFileInfo(marker).size() == 0) QFile::remove(marker);
}

QString logDirectory() { return directoryPath; }

QStringList pendingReports() {
    QStringList reports;
    const auto markers = QDir(directoryPath).entryInfoList({QStringLiteral("at-pet-*.log.crash")}, QDir::Files, QDir::Name);
    for (const auto &marker : markers) {
        if (marker.size() > 0 && marker.absoluteFilePath() != currentLog + QStringLiteral(".crash"))
            reports.append(marker.absoluteFilePath().chopped(6));
    }
    return reports;
}

bool exportReport(const QString &report, const QString &destination) {
    // Never overwrite either original when the user selects the source directory.
    const QFileInfo target(destination);
    if (target == QFileInfo(report) || target == QFileInfo(report + QStringLiteral(".crash"))) return false;
    QFile log(report);
    QFile crash(report + QStringLiteral(".crash"));
    QSaveFile output(destination);
    if (!log.open(QIODevice::ReadOnly) || !crash.open(QIODevice::ReadOnly)
        || !output.open(QIODevice::WriteOnly)) return false;
    const QByteArray data = log.readAll() + crash.readAll();
    return output.write(data) == data.size() && output.commit();
}

void acknowledgeReport(const QString &report) {
    // Keep the diagnostic details in the local log after a successful export.
    QFile crash(report + QStringLiteral(".crash"));
    QFile log(report);
    if (!crash.open(QIODevice::ReadOnly) || !log.open(QIODevice::Append)) return;
    const QByteArray details = crash.readAll();
    if (log.write(details) != details.size() || !log.flush()) return;
    crash.close();
    QFile::remove(crash.fileName());
}
}
