#include "petsettings.h"

#include <QSettings>

namespace petsettings {
namespace {
const QString lookAtMouseSettingKey = QStringLiteral("pet/lookAtMouse");
const QString legacyLookAtCopilotKey = QStringLiteral("pet/lookAtCopilot");
} // namespace

QString lookAtMouseKey() {
    return lookAtMouseSettingKey;
}

bool storedLookAtMouse(bool fallback) {
    return QSettings().value(lookAtMouseSettingKey, fallback).toBool();
}

bool loadLookAtMouse() {
    QSettings settings;
    if (settings.contains(lookAtMouseSettingKey))
        return settings.value(lookAtMouseSettingKey).toBool();
    const bool legacy = settings.value(legacyLookAtCopilotKey, false).toBool();
    settings.setValue(lookAtMouseSettingKey, legacy);
    settings.remove(legacyLookAtCopilotKey);
    return legacy;
}

void storeLookAtMouse(bool enabled) {
    QSettings().setValue(lookAtMouseSettingKey, enabled);
}

} // namespace petsettings
