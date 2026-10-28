#pragma once

#include <QString>

// Persisted pet preferences that are owned by PetController and rendered by SettingsDialog.
// Both sides go through this one implementation so the key names and the legacy migration
// cannot drift apart (they used to be duplicated verbatim in main.cpp and settingsdialog.cpp).
namespace petsettings {

// "pet/lookAtMouse"
QString lookAtMouseKey();

// Pure read of pet/lookAtMouse. Never writes. Used when a widget is only rendering state.
bool storedLookAtMouse(bool fallback = false);

// Reads pet/lookAtMouse, migrating the legacy "pet/lookAtCopilot" key on first run.
// This is the only function here that writes; call it from the controller at startup.
bool loadLookAtMouse();

// Persists pet/lookAtMouse.
void storeLookAtMouse(bool enabled);

} // namespace petsettings
