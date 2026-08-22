#pragma once

#include "platform/SettingsProvider.h"

namespace transmit::platform {

/// Reads and writes desktop preferences on Linux.
///
/// There is no single place these live: GNOME and its derivatives use
/// gsettings, KDE uses its own configuration files, and several settings are
/// system-wide rather than per-user. The provider detects the desktop and uses
/// whichever backend applies, falling back to the parts that are common to all
/// of them.
class LinuxSettingsProvider final : public SettingsProvider {
public:
    LinuxSettingsProvider();

    [[nodiscard]] QList<SettingValue> readAll() const override;
    [[nodiscard]] ApplyResult apply(const SettingValue& value) const override;
    [[nodiscard]] QString describeEnvironment() const override;

private:
    enum class Desktop { Unknown, Gnome, Kde, Xfce, Cinnamon, Mate, Lxqt };

    [[nodiscard]] static Desktop detectDesktop();
    [[nodiscard]] bool usesGSettings() const;

    Desktop desktop_ = Desktop::Unknown;
    QString desktopName_;
};

}  // namespace transmit::platform
