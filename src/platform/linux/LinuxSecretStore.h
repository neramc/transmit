#pragma once

#include "platform/SecretStore.h"

namespace transmit::platform {

/// Reads and writes credentials on Linux.
///
/// Wireless passphrases belong to NetworkManager and are readable only by root,
/// so they are requested through nmcli and reported as needing your permission
/// when that is refused rather than being quietly skipped. Application
/// passwords live in the login keyring, reached through libsecret's command
/// line tool so the build does not have to link against a desktop library that
/// may not be present.
class LinuxSecretStore final : public SecretStore {
public:
    [[nodiscard]] bool isAvailable() const override;
    [[nodiscard]] QString describe() const override;
    [[nodiscard]] QList<SecretRecord> read(bool includeWifi,
                                           bool includeApplications) const override;
    [[nodiscard]] ApplyResult store(const SecretRecord& record) const override;
};

}  // namespace transmit::platform
