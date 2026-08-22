#include "platform/macos/MacOsSecretStore.h"

#include <QProcess>
#include <QStandardPaths>

#include "core/utils/Logging.h"

#ifdef Q_OS_MACOS
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#endif

namespace transmit::platform {

bool MacOsSecretStore::isAvailable() const {
#ifdef Q_OS_MACOS
    return true;
#else
    return false;
#endif
}

QString MacOsSecretStore::describe() const { return QStringLiteral("the login keychain"); }

QList<SecretRecord> MacOsSecretStore::read(bool includeWifi, bool includeApplications) const {
    QList<SecretRecord> records;

#ifdef Q_OS_MACOS
    @autoreleasepool {
        // Generic passwords cover both application entries and the AirPort
        // items macOS uses for wireless networks.
        NSMutableDictionary* query = [NSMutableDictionary dictionary];
        query[(__bridge id)kSecClass] = (__bridge id)kSecClassGenericPassword;
        query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitAll;
        query[(__bridge id)kSecReturnAttributes] = @YES;
        query[(__bridge id)kSecReturnData] = @YES;

        CFTypeRef result = nullptr;
        const OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
        if (status != errSecSuccess) {
            if (status != errSecItemNotFound) {
                qCInfo(logSecrets) << "the keychain returned status" << status;
            }
            return records;
        }

        NSArray* items = (__bridge_transfer NSArray*)result;
        for (NSDictionary* item in items) {
            NSString* service = item[(__bridge id)kSecAttrService];
            NSData* data = item[(__bridge id)kSecValueData];
            if (service == nil || data == nil) {
                continue;
            }

            SecretRecord record;
            record.service = QString::fromNSString(service);
            record.label = record.service;

            NSString* account = item[(__bridge id)kSecAttrAccount];
            if (account != nil) {
                record.account = QString::fromNSString(account);
            }

            // macOS stores wireless passphrases under the AirPort service.
            const bool wifi =
                record.service.startsWith(QLatin1String("AirPort"), Qt::CaseInsensitive);
            record.kind = wifi ? SecretKind::WifiNetwork : SecretKind::ApplicationPassword;

            if ((wifi && !includeWifi) || (!wifi && !includeApplications)) {
                continue;
            }

            NSString* secret = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
            if (secret != nil) {
                record.secret = QString::fromNSString(secret);
                records.push_back(std::move(record));
            }
        }
    }
#else
    Q_UNUSED(includeWifi);
    Q_UNUSED(includeApplications);
#endif

    return records;
}

ApplyResult MacOsSecretStore::store(const SecretRecord& record) const {
#ifdef Q_OS_MACOS
    if (record.secret.isEmpty()) {
        return {ApplyOutcome::Failed, QStringLiteral("nothing was captured for this entry"), {}};
    }

    @autoreleasepool {
        NSData* secret = [record.secret.toNSString() dataUsingEncoding:NSUTF8StringEncoding];

        NSMutableDictionary* item = [NSMutableDictionary dictionary];
        item[(__bridge id)kSecClass] = (__bridge id)kSecClassGenericPassword;
        item[(__bridge id)kSecAttrService] = record.service.toNSString();
        item[(__bridge id)kSecAttrLabel] = record.label.toNSString();
        if (!record.account.isEmpty()) {
            item[(__bridge id)kSecAttrAccount] = record.account.toNSString();
        }
        item[(__bridge id)kSecValueData] = secret;

        OSStatus status = SecItemAdd((__bridge CFDictionaryRef)item, nullptr);

        if (status == errSecDuplicateItem) {
            // Already there under the same service and account; update it
            // rather than leaving the old value in place.
            NSMutableDictionary* query = [NSMutableDictionary dictionary];
            query[(__bridge id)kSecClass] = (__bridge id)kSecClassGenericPassword;
            query[(__bridge id)kSecAttrService] = record.service.toNSString();
            if (!record.account.isEmpty()) {
                query[(__bridge id)kSecAttrAccount] = record.account.toNSString();
            }

            NSDictionary* update = @{(__bridge id)kSecValueData : secret};
            status =
                SecItemUpdate((__bridge CFDictionaryRef)query, (__bridge CFDictionaryRef)update);
        }

        if (status == errSecSuccess) {
            return {ApplyOutcome::Applied, {}, {}};
        }
        return {ApplyOutcome::Failed,
                QStringLiteral("the keychain refused it (status %1)").arg(status),
                {}};
    }
#else
    Q_UNUSED(record);
    return {ApplyOutcome::Unsupported, {}, {}};
#endif
}

}  // namespace transmit::platform
