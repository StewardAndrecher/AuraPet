#include "chat_credentials.h"
#include "settings_store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {
constexpr auto filePermissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
constexpr int maxKeyBytes = 16384;
constexpr char dpapiMagic[] = "APDP1:";

bool storageError(QString& error)
{
    error = QStringLiteral("无法访问 AuraPet 的本机密钥存储。请检查当前账户的文件权限。");
    return false;
}

bool credentialPath(const QString& endpoint, bool create, QString& path, QString& error)
{
    const auto root = SettingsStore::dataPath();
    if (root.isEmpty() || (create && !QDir().mkpath(root))) {
        return storageError(error);
    }
    const auto directory = QDir(root).filePath(QStringLiteral("chat-credentials"));
    if (create && !QDir().mkpath(directory)) {
        return storageError(error);
    }
    if (!QDir(directory).exists()) {
        return !create ? true : storageError(error);
    }
    const auto id = QCryptographicHash::hash(endpoint.toUtf8(), QCryptographicHash::Sha256).toHex();
    path = QDir(directory).filePath(QString::fromLatin1(id));
    return true;
}

#ifdef Q_OS_WIN
bool protectBytes(const QByteArray& plain, QByteArray& encrypted)
{
    DATA_BLOB inBlob{};
    inBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()));
    inBlob.cbData = static_cast<DWORD>(plain.size());
    DATA_BLOB outBlob{};
    if (!CryptProtectData(&inBlob, L"AuraPetChatKey", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
        return false;
    }
    encrypted = QByteArray(dpapiMagic, static_cast<qsizetype>(sizeof(dpapiMagic) - 1));
    encrypted.append(reinterpret_cast<const char*>(outBlob.pbData), static_cast<qsizetype>(outBlob.cbData));
    LocalFree(outBlob.pbData);
    return true;
}

bool unprotectBytes(const QByteArray& stored, QByteArray& plain)
{
    const QByteArray prefix(dpapiMagic, static_cast<qsizetype>(sizeof(dpapiMagic) - 1));
    if (!stored.startsWith(prefix)) {
        plain = stored;
        return true;
    }
    const QByteArray cipher = stored.mid(prefix.size());
    DATA_BLOB inBlob{};
    inBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(cipher.constData()));
    inBlob.cbData = static_cast<DWORD>(cipher.size());
    DATA_BLOB outBlob{};
    if (!CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
        return false;
    }
    plain = QByteArray(reinterpret_cast<const char*>(outBlob.pbData), static_cast<qsizetype>(outBlob.cbData));
    SecureZeroMemory(outBlob.pbData, outBlob.cbData);
    LocalFree(outBlob.pbData);
    return true;
}
#endif
}

bool readChatKey(const QString& endpoint, bool local, QString& key, QString& error)
{
    Q_UNUSED(local);
    key.clear();
    QString path;
    if (!credentialPath(endpoint, false, path, error)) {
        return false;
    }
    if (path.isEmpty() || !QFile::exists(path)) {
        return true;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return storageError(error);
    }
    const auto raw = file.read(maxKeyBytes * 4 + 1);
    if (file.error() != QFileDevice::NoError || raw.size() > maxKeyBytes * 4) {
        return storageError(error);
    }
#ifdef Q_OS_WIN
    QByteArray plain;
    if (!unprotectBytes(raw, plain) || plain.size() > maxKeyBytes) {
        return storageError(error);
    }
    key = QString::fromUtf8(plain);
#else
    if (raw.size() > maxKeyBytes) {
        return storageError(error);
    }
    key = QString::fromUtf8(raw);
#endif
    return true;
}

bool saveChatKey(const QString& endpoint, const QString& key, QString& error)
{
    QString path;
    if (!credentialPath(endpoint, !key.isEmpty(), path, error)) {
        return false;
    }
    if (key.isEmpty()) {
        return path.isEmpty() || !QFile::exists(path) || QFile::remove(path) || storageError(error);
    }
    const auto bytes = key.toUtf8();
    if (bytes.size() > maxKeyBytes || bytes.contains('\r') || bytes.contains('\n') || bytes.contains('\0')) {
        error = QStringLiteral("API Key 格式无效。请粘贴单行密钥。");
        return false;
    }
    QByteArray payload = bytes;
#ifdef Q_OS_WIN
    if (!protectBytes(bytes, payload)) {
        return storageError(error);
    }
#endif
    QSaveFile file(path);
    return (file.open(QIODevice::WriteOnly) && file.setPermissions(filePermissions)
        && file.write(payload) == payload.size() && file.commit()) || storageError(error);
}
