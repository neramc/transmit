#pragma once

#include "platform/SecretStore.h"

namespace transmit::platform {

/// Reads and writes credentials on Windows.
///
/// Credential Manager entries are sealed with DPAPI against the signed-in
/// account, so they can be read here but are meaningless to any other machine
/// until they are re-stored on it. Wireless profiles are exported through netsh,
/// which will only reveal a key to an administrator.
class WindowsSecretStore final : public SecretStore {
public:
    [[nodiscard]] bool isAvailable() const override;
    [[nodiscard]] QString describe() const override;
    [[nodiscard]] QList<SecretRecord> read(bool includeWifi,
                                           bool includeApplications) const override;
    [[nodiscard]] ApplyResult store(const SecretRecord& record) const override;
};

}  // namespace transmit::platform
