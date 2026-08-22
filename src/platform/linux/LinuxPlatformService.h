#pragma once

#include "platform/PlatformService.h"

namespace transmit::platform {

/// Linux implementation, covering the desktop distributions Transmit targets:
/// Debian and Ubuntu derivatives (Mint, Pop!_OS, Raspberry Pi OS), Fedora and
/// RHEL derivatives, openSUSE, Arch, Gentoo, NixOS, Alpine, Void, Slackware and
/// ChromiumOS's Crostini container.
class LinuxPlatformService final : public PlatformService {
public:
    [[nodiscard]] EnvironmentInfo environment() const override;
    [[nodiscard]] PathTokenMap knownFolders() const override;
    [[nodiscard]] QList<StorageVolume> storageVolumes() const override;
    [[nodiscard]] QList<InstalledApp> installedApplications() const override;
    [[nodiscard]] QList<RunningApp> runningApplications(
        const QStringList& processNames) const override;
    [[nodiscard]] std::unique_ptr<Snapshot> createSnapshot(const QStringList& paths) const override;
    [[nodiscard]] QString packageInstallCommand() const override;
    [[nodiscard]] PackageSource nativePackageSource() const override;
    [[nodiscard]] std::unique_ptr<SettingsProvider> settingsProvider() const override;
};

/// Reads /etc/os-release into a key/value map. Exposed for testing and for the
/// application-catalog matcher, which needs ID and ID_LIKE.
QHash<QString, QString> readOsRelease(const QString& path = QStringLiteral("/etc/os-release"));

/// Maps an os-release ID (plus ID_LIKE) onto the distribution's package
/// manager. Derivatives fall back to their parent, so Mint resolves to apt and
/// Rocky to dnf without needing an entry of their own.
PackageSource packageSourceForDistro(const QString& id, const QString& idLike);

}  // namespace transmit::platform
