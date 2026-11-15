#include "apicredentials.h"
#include "agentregistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <aclapi.h>
#include <sddl.h>
#endif

namespace {
bool protectDirectory(const QString &path) {
#ifdef Q_OS_WIN
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    QByteArray buffer(int(size), '\0');
    const bool obtained = GetTokenInformation(token, TokenUser, buffer.data(), size, &size);
    CloseHandle(token);
    if (!obtained) return false;
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &sid)) return false;
    const QString sddl = QStringLiteral("D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;%1)").arg(QString::fromWCharArray(sid));
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            reinterpret_cast<LPCWSTR>(sddl.utf16()), SDDL_REVISION_1, &descriptor, nullptr)) return false;
    PACL acl = nullptr;
    BOOL present = FALSE, defaulted = FALSE;
    const bool valid = GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted);
    const DWORD result = valid && present
        ? SetNamedSecurityInfoW(reinterpret_cast<LPWSTR>(const_cast<ushort *>(path.utf16())),
              SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
              nullptr, nullptr, acl, nullptr) : ERROR_INVALID_SECURITY_DESCR;
    LocalFree(descriptor);
    return result == ERROR_SUCCESS;
#else
    return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
#endif
}

QByteArray newToken() {
    QByteArray bytes(32, '\0');
    for (int i = 0; i < bytes.size(); i += 4) {
        const quint32 value = QRandomGenerator::system()->generate();
        for (int j = 0; j < 4; ++j) bytes[i + j] = char(value >> (j * 8));
    }
    return bytes.toHex();
}
}

QString apicredentials::defaultPath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("api-security/clients.json"));
}

bool apicredentials::loadOrCreate(QList<ApiClient> *clients, QString *error, const QString &path) {
    clients->clear();
    const QString directory = QFileInfo(path).absolutePath();
    if (QFileInfo(directory).isSymLink() || QFileInfo(path).isSymLink() ||
        !QDir().mkpath(directory) || !protectDirectory(QDir::toNativeSeparators(directory))) {
        *error = QStringLiteral("Could not secure the local API credential directory");
        return false;
    }
    QLockFile lock(QDir(directory).filePath(QStringLiteral("clients.lock")));
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(2000)) {
        *error = QStringLiteral("Local API credentials are locked by another process");
        return false;
    }
    if (!QFileInfo::exists(path)) {
        QJsonArray entries;
        entries.append(QJsonObject{{"id", "admin"}, {"administrator", true},
                                   {"token", QString::fromLatin1(newToken())}});
        QSet<QString> keys{QStringLiteral("external")};
        for (const auto &agent : agentDefinitions()) keys.insert(agent.key);
        for (const auto &key : keys)
            entries.append(QJsonObject{{"id", key}, {"agentKey", key},
                                       {"token", QString::fromLatin1(newToken())}});
        QSaveFile output(path);
        const QByteArray data = QJsonDocument(QJsonObject{{"version", 1}, {"clients", entries}}).toJson();
        if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit()) {
            *error = QStringLiteral("Could not save local API credentials");
            return false;
        }
#ifndef Q_OS_WIN
        if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            *error = QStringLiteral("Could not protect local API credentials");
            return false;
        }
#endif
    }
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly) || input.size() > 1024 * 1024) {
        *error = QStringLiteral("Could not read local API credentials");
        return false;
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(input.readAll(), &parse);
    const auto root = document.object();
    QSet<QString> ids;
    QSet<QByteArray> tokens;
    QList<ApiClient> parsed;
    bool valid = parse.error == QJsonParseError::NoError && root.value("version").toInt() == 1 &&
        root.value("clients").isArray() && root.value("clients").toArray().size() <= 256;
    for (const auto &value : root.value("clients").toArray()) {
        const auto row = value.toObject();
        ApiClient client{row.value("id").toString().trimmed(), row.value("agentKey").toString().trimmed(),
                         row.value("token").toString().toLatin1(), row.value("administrator").toBool()};
        static const QRegularExpression identifier(QStringLiteral("^[A-Za-z0-9._-]{1,128}$"));
        static const QRegularExpression credential(QStringLiteral("^[A-Za-z0-9_-]{32,256}$"));
        if (!identifier.match(client.id).hasMatch() || ids.contains(client.id) ||
            !credential.match(row.value("token").toString()).hasMatch() || tokens.contains(client.token) ||
            (!client.administrator && (client.agentKey.isEmpty() || agentAppKey(client.agentKey) != client.agentKey))) {
            valid = false;
            break;
        }
        ids.insert(client.id);
        tokens.insert(client.token);
        parsed.append(client);
    }
    if (!valid || parsed.isEmpty()) {
        *error = QStringLiteral("Invalid local API credential store; API remains disabled");
        return false;
    }
    *clients = parsed;
    error->clear();
    return true;
}
