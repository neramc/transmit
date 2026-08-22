#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include <memory>

#include "platform/SettingsProvider.h"

namespace transmit::platform {

/// What kind of thing a stored secret is, which decides where it goes on the
/// far side.
enum class SecretKind {
    WifiNetwork,          ///< a wireless network and its passphrase
    ApplicationPassword,  ///< an entry an application put in the keychain
    NetworkCredential,    ///< a saved login for a server or share
    BrowserLogin,         ///< a site login held by a browser
};

QString secretKindName(SecretKind kind);

/// One credential, in the form it travels.
///
/// The `secret` field is the only plaintext copy that ever exists outside the
/// operating system's own store, it lives in memory for the length of one
/// capture, and it is written only into an archive that is already encrypted.
/// It is never logged, never printed, and never touches a temporary file.
struct SecretRecord {
    SecretKind kind = SecretKind::ApplicationPassword;
    QString service;  ///< network name, host or application
    QString account;  ///< user name, where there is one
    QString secret;
    QString label;  ///< what the user would recognise it as
    QHash<QString, QString> attributes;

    /// Overwrites the secret in place. Called as soon as it has been handed on.
    void clear();
};

/// Reads and writes the operating system's credential store.
///
/// These stores are the one thing a file copy genuinely cannot move: Windows
/// seals them with DPAPI against the machine account, macOS with the Keychain,
/// Linux with the login keyring. Carrying them requires decrypting on one side
/// and re-encrypting on the other, which is why this is opt-in, why the archive
/// must be encrypted, and why the user is told plainly what is in it.
class SecretStore {
public:
    virtual ~SecretStore() = default;

    /// False when this build or this system cannot reach the credential store.
    [[nodiscard]] virtual bool isAvailable() const = 0;

    /// Names the store, for the report: "GNOME Keyring", "Windows Credential
    /// Manager", "login keychain".
    [[nodiscard]] virtual QString describe() const = 0;

    /// Reads what the user asked for. Reading may prompt: macOS asks the user
    /// to approve keychain access, and Linux may need the keyring unlocked.
    [[nodiscard]] virtual QList<SecretRecord> read(bool includeWifi,
                                                   bool includeApplications) const = 0;

    /// Puts one credential into this system's store.
    [[nodiscard]] virtual ApplyResult store(const SecretRecord& record) const = 0;
};

}  // namespace transmit::platform
