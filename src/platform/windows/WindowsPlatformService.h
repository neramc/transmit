#pragma once

#include "platform/PlatformService.h"

namespace transmit::platform {

/// Windows 10 1809 and later. Windows 8.1 is out of scope because Qt 6 does
/// not support it; see the README's platform table.
class WindowsPlatformService final : public PlatformService {
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
    [[nodiscard]] std::unique_ptr<SecretStore> secretStore() const override;

protected:
    [[nodiscard]] QString unmountVolume(const QString& rootPath) const override;
};

}  // namespace transmit::platform
