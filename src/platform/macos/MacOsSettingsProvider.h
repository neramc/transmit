#pragma once

#include "platform/SettingsProvider.h"

namespace transmit::platform {

/// Reads and writes user preferences on macOS through the defaults system.
///
/// macOS expresses several of these by absence rather than by value - there is
/// no "light mode" key, only the lack of AppleInterfaceStyle - so reading and
/// writing both have to account for a key that is not there.
class MacOsSettingsProvider final : public SettingsProvider {
public:
    [[nodiscard]] QList<SettingValue> readAll() const override;
    [[nodiscard]] ApplyResult apply(const SettingValue& value) const override;
    [[nodiscard]] QString describeEnvironment() const override;
};

}  // namespace transmit::platform
