#pragma once

#include "platform/SecretStore.h"

namespace transmit::platform {

/// Reads and writes credentials in the macOS login keychain.
///
/// macOS asks the user to approve each read, by design. That prompt is the
/// point: it is the system telling them something is about to take their
/// passwords out of the keychain, which is exactly what is happening.
class MacOsSecretStore final : public SecretStore {
public:
    [[nodiscard]] bool isAvailable() const override;
    [[nodiscard]] QString describe() const override;
    [[nodiscard]] QList<SecretRecord> read(bool includeWifi,
                                           bool includeApplications) const override;
    [[nodiscard]] ApplyResult store(const SecretRecord& record) const override;
};

}  // namespace transmit::platform
