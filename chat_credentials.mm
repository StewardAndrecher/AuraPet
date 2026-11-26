#include "chat_credentials.h"
#include "settings_store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr auto filePermissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
constexpr int maxKeyBytes = 16384;

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
    const auto name = QFile::encodeName(directory);
    if (create && mkdir(name.constData(), S_IRWXU) != 0 && errno != EEXIST) {
        return storageError(error);
    }
    struct stat info{};
    if (lstat(name.constData(), &info) != 0) {
        return errno == ENOENT && !create ? true : storageError(error);
    }
    if (!S_ISDIR(info.st_mode) || info.st_uid != geteuid() || (info.st_mode & (S_IRWXG | S_IRWXO))) {
        return storageError(error);
    }
    const auto id = QCryptographicHash::hash(endpoint.toUtf8(), QCryptographicHash::Sha256).toHex();
    path = QDir(directory).filePath(QString::fromLatin1(id));
    return true;
}

bool readLegacyKey(const QString& endpoint, QString& key)
{
    @autoreleasepool {
        Boolean wasAllowed = false;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        if (SecKeychainGetUserInteractionAllowed(&wasAllowed) != errSecSuccess
            || SecKeychainSetUserInteractionAllowed(false) != errSecSuccess) {
            return false;
        }
        const NSDictionary* query = @{
            (__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
            (__bridge id)kSecAttrService: @"com.gifpet.desktop.chat-api",
            (__bridge id)kSecAttrAccount: endpoint.toNSString(),
            (__bridge id)kSecReturnData: @YES,
            (__bridge id)kSecMatchLimit: (__bridge id)kSecMatchLimitOne,
        };
        CFTypeRef raw = nullptr;
        const auto status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &raw);
        SecKeychainSetUserInteractionAllowed(wasAllowed);
#pragma clang diagnostic pop
        NSData* data = CFBridgingRelease(raw);
        if (status != errSecSuccess || !data || data.length > maxKeyBytes) {
            return false;
        }
        key = QString::fromUtf8(static_cast<const char*>(data.bytes), static_cast<qsizetype>(data.length));
        return !key.isEmpty();
    }
}
}

bool readChatKey(const QString& endpoint, bool local, QString& key, QString& error)
{
    key.clear();
    if (!local) {
        if (!readLegacyKey(endpoint, key)) {
            error = QStringLiteral("旧版钥匙串中的 API Key 无法静默读取。请在「AI 接口」重新填入并保存一次。");
            return false;
        }
        return true;
    }
    QString path;
    if (!credentialPath(endpoint, false, path, error)) {
        return false;
    }
    if (path.isEmpty()) {
        return true;
    }
    const int fd = open(QFile::encodeName(path).constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return errno == ENOENT ? true : storageError(error);
    }
    struct stat info{};
    QFile file;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != geteuid()
        || (info.st_mode & (S_IRWXG | S_IRWXO)) || info.st_size > maxKeyBytes
        || !file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        close(fd);
        return storageError(error);
    }
    const auto bytes = file.read(maxKeyBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > maxKeyBytes) {
        return storageError(error);
    }
    key = QString::fromUtf8(bytes);
    return true;
}

bool saveChatKey(const QString& endpoint, const QString& key, QString& error)
{
    QString path;
    if (!credentialPath(endpoint, !key.isEmpty(), path, error)) {
        return false;
    }
    const auto filename = QFile::encodeName(path);
    if (key.isEmpty()) {
        return path.isEmpty() || unlink(filename.constData()) == 0 || errno == ENOENT || storageError(error);
    }
    struct stat info{};
    const int result = lstat(filename.constData(), &info);
    if (result == 0 ? !S_ISREG(info.st_mode) || info.st_uid != geteuid() : errno != ENOENT) {
        return storageError(error);
    }
    const auto bytes = key.toUtf8();
    if (bytes.size() > maxKeyBytes || bytes.contains('\r') || bytes.contains('\n') || bytes.contains('\0')) {
        error = QStringLiteral("API Key 格式无效。请粘贴单行密钥。");
        return false;
    }
    QSaveFile file(path);
    return (file.open(QIODevice::WriteOnly) && file.setPermissions(filePermissions)
        && file.write(bytes) == bytes.size() && file.commit()) || storageError(error);
}
