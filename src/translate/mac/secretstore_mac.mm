#include "translate/secretstore.h"

#include <QObject>

#import <Foundation/Foundation.h>
#import <Security/Security.h>

namespace {
NSString* const kService = @"com.github.wangchaozhi.SnapPin";

NSMutableDictionary* query(const QString& key)
{
    return [@{
        (__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService: kService,
        (__bridge id)kSecAttrAccount: key.toNSString(),
    } mutableCopy];
}
} // namespace

QString readSecret(const QString& key)
{
    NSMutableDictionary* q = query(key);
    q[(__bridge id)kSecReturnData] = @YES;
    q[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
    CFTypeRef result = nullptr;
    if (SecItemCopyMatching((__bridge CFDictionaryRef)q, &result) != errSecSuccess || !result)
        return QString();
    NSData* data = (__bridge_transfer NSData*)result;
    return QString::fromUtf8(static_cast<const char*>(data.bytes), qsizetype(data.length));
}

bool writeSecret(const QString& key, const QString& value)
{
    NSMutableDictionary* q = query(key);
    SecItemDelete((__bridge CFDictionaryRef)q);
    if (value.isEmpty())
        return true;
    const QByteArray utf8 = value.toUtf8();
    q[(__bridge id)kSecValueData] = [NSData dataWithBytes:utf8.constData() length:NSUInteger(utf8.size())];
    q[(__bridge id)kSecAttrLabel] = [NSString stringWithFormat:@"SnapPin: %@", key.toNSString()];
    return SecItemAdd((__bridge CFDictionaryRef)q, nullptr) == errSecSuccess;
}

QString secretStoreName()
{
    return QObject::tr("macOS 钥匙串");
}
