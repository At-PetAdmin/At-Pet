#include "crashreporter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

class CrashReporterTest : public QObject {
    Q_OBJECT
private:
    int runProbe(const QString &directory, const QString &mode) {
        QProcess process;
        process.setProgram(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("crash-probe")));
        process.setArguments({directory, mode});
        process.start();
        if (!process.waitForStarted() || !process.waitForFinished(15000)) return -1;
        return process.exitCode();
    }
    QByteArray readFile(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
private slots:
    void cleanShutdown() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QCOMPARE(runProbe(directory.path(), "clean"), 0);
        QVERIFY(crashreporter::start(directory.path()));
        QVERIFY(crashreporter::pendingReports().isEmpty());
        crashreporter::stop();
        const QStringList files = QDir(directory.path()).entryList({"*.log"});
        QCOMPARE(files.size(), 2);
        bool warningFound = false;
        for (const QString &file : files) {
            const QByteArray data = readFile(QDir(directory.path()).filePath(file));
            QVERIFY(data.contains("Clean shutdown"));
            warningFound |= data.contains("crash probe warning");
        }
        QVERIFY(warningFound);
        QVERIFY(QDir(directory.path()).entryList({"*.crash"}).isEmpty());
    }
    void crashRecovery_data() {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QByteArray>("reason");
        QTest::newRow("Qt fatal") << QString("fatal") << QByteArray("crash probe fatal");
        QTest::newRow("abort") << QString("abort") << QByteArray("SIGABRT");
        QTest::newRow("C++ termination") << QString("terminate") << QByteArray("std::terminate");
#ifdef Q_OS_WIN
        QTest::newRow("Windows exception") << QString("seh") << QByteArray("C0000005");
#endif
        QTest::newRow("bounded log") << QString("limit") << QByteArray("fatal after log limit");
    }
    void crashRecovery() {
        QFETCH(QString, mode);
        QFETCH(QByteArray, reason);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const int result = runProbe(directory.path(), mode);
        QVERIFY(result != -1);
        QVERIFY(result != 0);
        QVERIFY(crashreporter::start(directory.path()));
        const QStringList reports = crashreporter::pendingReports();
        QCOMPARE(reports.size(), 1);
        const QString report = reports.first();
        QVERIFY(QFileInfo(report).size() <= 1024 * 1024 + 1024);
        QVERIFY(!crashreporter::exportReport(report, report));
        QVERIFY(!crashreporter::exportReport(report, report + ".crash"));
#ifdef Q_OS_WIN
        QVERIFY(!crashreporter::exportReport(report, report.toUpper()));
#endif
        QVERIFY(!crashreporter::exportReport(report, directory.filePath("missing/export.log")));
        QCOMPARE(crashreporter::pendingReports().size(), 1);
        const QString destination = directory.filePath(QString::fromUtf8("导出的错误日志.log"));
        QVERIFY(crashreporter::exportReport(report, destination));
        const QByteArray exported = readFile(destination);
        QVERIFY(exported.contains("At-Pet"));
        QVERIFY(exported.contains("Architecture:"));
        QVERIFY(exported.contains("crash probe warning"));
        QVERIFY(exported.contains(reason));
        crashreporter::acknowledgeReport(report);
        QVERIFY(crashreporter::pendingReports().isEmpty());
        QVERIFY(readFile(report).contains(reason));
        crashreporter::stop();
    }
    void retainsPendingReports() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        for (int i = 0; i < 12; ++i) {
            QFile file(directory.filePath(QString("at-pet-%1.log").arg(i, 2, 10, QLatin1Char('0'))));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("old session\n");
        }
        const QString pending = directory.filePath("at-pet-00.log");
        QFile marker(pending + ".crash");
        QVERIFY(marker.open(QIODevice::WriteOnly));
        marker.write("crash details\n");
        marker.close();
        QVERIFY(crashreporter::start(directory.path()));
        QVERIFY(QFile::exists(pending));
        QVERIFY(!QFile::exists(directory.filePath("at-pet-01.log")));
        QCOMPARE(crashreporter::pendingReports(), QStringList{pending});
        // Ten retained ordinary logs, one pending report, plus the current session.
        QCOMPARE(QDir(directory.path()).entryList({"*.log"}).size(), 12);
        crashreporter::stop();
    }
    void cleanup() { crashreporter::stop(); }
};

QTEST_GUILESS_MAIN(CrashReporterTest)
#include "tst_crashreporter.moc"
