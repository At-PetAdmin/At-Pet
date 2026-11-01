#pragma once

#include <QString>

// Pet ids are directory names inside the pet library (see PetController::switchPet()).
// They reach the controller from the UI and from the unauthenticated local HTTP API
// (/summon { "petId": ... }), so an untrusted value must be checked before QDir::filePath()
// turns it into a path. Nothing a filesystem treats as a separator or a parent reference is
// accepted; an empty id means the built-in default pet.
namespace petid {

inline bool isSafe(const QString &id) {
    if (id.isEmpty())
        return true;
    if (id == QLatin1String(".") || id == QLatin1String(".."))
        return false;
    for (const QChar character : id) {
        if (character == QLatin1Char('/') || character == QLatin1Char('\\') ||
            character == QLatin1Char(':') || character.unicode() < 0x20)
            return false;
    }
    return true;
}

} // namespace petid
