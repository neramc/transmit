#pragma once

#include <memory>
#include <utility>

#include "platform/PlatformService.h"

namespace transmit::testing {

/// A platform service that is the real one except for the drives.
///
/// Written as a decorator rather than a stub: a test about what happens on a
/// write-protected stick should be running against this machine's real folder
/// table, real applications and real snapshots, and differ from an ordinary
/// run in exactly the one thing it is about. A stub would answer every
/// question with something invented, and then the test would be measuring the
/// stub.
class PlatformWithDrives : public platform::PlatformService {
public:
    PlatformWithDrives(std::unique_ptr<platform::PlatformService> real,
                       QList<platform::StorageVolume> volumes)
        : real_(std::move(real)), volumes_(std::move(volumes)) {}

    [[nodiscard]] QList<platform::StorageVolume> storageVolumes() const override {
        return volumes_;
    }

    // Everything else is whatever this machine says.
    [[nodiscard]] platform::EnvironmentInfo environment() const override {
        return real_->environment();
    }
    [[nodiscard]] format::PathTokenMap knownFolders() const override {
        return real_->knownFolders();
    }
    [[nodiscard]] QList<platform::InstalledApp> installedApplications() const override {
        return real_->installedApplications();
    }
    [[nodiscard]] QList<platform::RunningApp> runningApplications(
        const QStringList& processNames) const override {
        return real_->runningApplications(processNames);
    }
    [[nodiscard]] std::unique_ptr<platform::Snapshot> createSnapshot(
        const QStringList& paths) const override {
        return real_->createSnapshot(paths);
    }
    [[nodiscard]] QString packageInstallCommand() const override {
        return real_->packageInstallCommand();
    }
    [[nodiscard]] platform::PackageSource nativePackageSource() const override {
        return real_->nativePackageSource();
    }
    [[nodiscard]] std::unique_ptr<platform::SettingsProvider> settingsProvider() const override {
        return real_->settingsProvider();
    }
    [[nodiscard]] std::unique_ptr<platform::SecretStore> secretStore() const override {
        return real_->secretStore();
    }

private:
    std::unique_ptr<platform::PlatformService> real_;
    QList<platform::StorageVolume> volumes_;
};

}  // namespace transmit::testing
