#pragma once

#include "platform/PlatformService.h"

namespace transmit::platform {

/// macOS 14 Sonoma and later.
///
/// Several of the most valuable locations - Mail, Safari, Messages - sit behind
/// the Full Disk Access privacy control. The capture reports what it could not
/// read rather than failing, and the UI points the user at System Settings.
class MacOsPlatformService final : public PlatformService {
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

    /// True when the process can read the protected user data locations. The
    /// UI uses this to show the Full Disk Access prompt before a capture.
    [[nodiscard]] static bool hasFullDiskAccess();
};

}  // namespace transmit::platform
