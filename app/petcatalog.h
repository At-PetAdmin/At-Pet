#pragma once

#include "petid.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

// Resource-backed pets and imported pets share IDs across settings, menus and the API.
// The reserved prefix keeps shipped pets separate from existing imported folder names.
namespace petcatalog {
inline QStringList bundledIds() {
    return {QStringLiteral("builtin-chatgpt"), QStringLiteral("builtin-claude"),
            QStringLiteral("builtin-deepseek"), QStringLiteral("builtin-gemini")};
}

inline bool isBuiltin(const QString &id) {
    return id.isEmpty() || id == QLatin1String("default") || bundledIds().contains(id);
}

inline QString libraryPath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("pets");
}

inline QString directory(const QString &id) {
    if (id.isEmpty() || id == QLatin1String("default") || !petid::isSafe(id)) return {};
    return bundledIds().contains(id) ? QStringLiteral(":/pets/") + id
                                    : QDir(libraryPath()).filePath(id);
}

inline QString name(const QString &id) {
    if (id == QLatin1String("builtin-chatgpt")) return QStringLiteral("ChatGPT");
    if (id == QLatin1String("builtin-claude")) return QStringLiteral("Claude");
    if (id == QLatin1String("builtin-deepseek")) return QStringLiteral("DeepSeek");
    if (id == QLatin1String("builtin-gemini")) return QStringLiteral("Gemini");
    return id.isEmpty() || id == QLatin1String("default") ? QStringLiteral("Octocat") : id;
}

inline QStringList ids() {
    QStringList result{QStringLiteral("default")};
    result.append(bundledIds());
    const auto imported = QDir(libraryPath()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto &pet : imported) {
        if (!isBuiltin(pet.fileName()) && petid::isSafe(pet.fileName())
            && QFileInfo::exists(QDir(pet.absoluteFilePath()).filePath("pet.json")))
            result.append(pet.fileName());
    }
    return result;
}
}
