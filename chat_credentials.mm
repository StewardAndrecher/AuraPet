#include "chat_credentials.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

namespace {
NSDictionary* entry(const QString& endpoint)
{
    return @{(__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
             (__bridge id)kSecAttrService: @"com.gifpet.desktop.chat-api",
             (__bridge id)kSecAttrAccount: endpoint.toNSString()};
}

bool resultFor(OSStatus status, QString& error)
{
    if (status == errSecSuccess) {
        return true;
    }
    NSString* detail = CFBridgingRelease(SecCopyErrorMessageString(status, nullptr));
    error = QStringLiteral("无法访问 macOS 钥匙串：%1")
        .arg(detail ? QString::fromNSString(detail) : QString::number(status));
    return false;
}
}

bool readChatKey(const QString& endpoint, QString& key, QString& error)
{
    @autoreleasepool {
        NSMutableDictionary* attributes = [entry(endpoint) mutableCopy];
        attributes[(__bridge id)kSecReturnData] = @YES;
        attributes[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
        CFTypeRef raw = nullptr;
        const OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)attributes, &raw);
        key.clear();
        if (status == errSecItemNotFound) {
            return true;
        }
        if (!resultFor(status, error)) {
            return false;
        }
        NSData* data = CFBridgingRelease(raw);
        key = QString::fromUtf8(static_cast<const char*>(data.bytes), static_cast<qsizetype>(data.length));
        return true;
    }
}

bool saveChatKey(const QString& endpoint, const QString& key, QString& error)
{
    @autoreleasepool {
        NSDictionary* attributes = entry(endpoint);
        if (key.isEmpty()) {
            const OSStatus status = SecItemDelete((__bridge CFDictionaryRef)attributes);
            return status == errSecItemNotFound || resultFor(status, error);
        }
        const QByteArray bytes = key.toUtf8();
        NSData* data = [NSData dataWithBytes:bytes.constData() length:static_cast<NSUInteger>(bytes.size())];
        NSDictionary* values = @{(__bridge id)kSecValueData: data};
        OSStatus status = SecItemUpdate((__bridge CFDictionaryRef)attributes, (__bridge CFDictionaryRef)values);
        if (status == errSecItemNotFound) {
            NSMutableDictionary* inserted = [attributes mutableCopy];
            [inserted addEntriesFromDictionary:values];
            status = SecItemAdd((__bridge CFDictionaryRef)inserted, nullptr);
        }
        return resultFor(status, error);
    }
}
