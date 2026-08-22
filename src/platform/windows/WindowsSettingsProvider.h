#pragma once

#include "platform/SettingsProvider.h"

namespace transmit::platform {

/// Reads and writes user preferences on Windows.
///
/// Most of these live in HKEY_CURRENT_USER. Two do not behave like ordinary
/// settings and are handled honestly rather than pretended at: the default
/// browser is protected by a hash Windows computes itself, and the time zone
/// is machine-wide. Both come back as work for the user, with the exact
/// command or the Settings page that does it.
class WindowsSettingsProvider final : public SettingsProvider {
public:
    [[nodiscard]] QList<SettingValue> readAll() const override;
    [[nodiscard]] ApplyResult apply(const SettingValue& value) const override;
    [[nodiscard]] QString describeEnvironment() const override;
};

}  // namespace transmit::platform
