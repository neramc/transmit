#include "platform/SecretStore.h"

#include <QCoreApplication>

namespace transmit::platform {

QString secretKindName(SecretKind kind) {
    switch (kind) {
        case SecretKind::WifiNetwork:
            return QCoreApplication::translate("Secrets", "Wi-Fi network");
        case SecretKind::ApplicationPassword:
            return QCoreApplication::translate("Secrets", "Application password");
        case SecretKind::NetworkCredential:
            return QCoreApplication::translate("Secrets", "Saved login");
        case SecretKind::BrowserLogin:
            return QCoreApplication::translate("Secrets", "Website login");
    }
    return {};
}

void SecretRecord::clear() {
    // QString cannot promise the bytes are gone - it may have been copied on
    // write - but overwriting the buffer we hold removes the obvious copy, and
    // the record is destroyed immediately afterwards.
    secret.fill(QChar(u'\0'));
    secret.clear();
}

}  // namespace transmit::platform
