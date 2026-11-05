#pragma once

#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

// Exercise the real QSettings API without depending on the user's registry permissions or
// leaving test preferences in their profile. The directory outlives every test in the suite.
inline void isolateTestSettings() {
    static QTemporaryDir directory;
    QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, directory.path());
}
