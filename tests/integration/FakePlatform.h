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

    /// Answer environment() with a different operating system.
    ///
    /// For the rules that differ per system and are read off files rather than
    /// out of an API - what an evicted iCloud file is called, say. Without this
    /// each of those could only be exercised on the machine that has it, which
    /// means on one of the three CI runners and never in a developer's loop.
    void pretendToBe(format::OsFamily os) { pretend_ = os; }

    /// Answer accessObstacles() with something in the way.
    ///
    /// Each real one needs a machine in a particular state - a macOS without
    /// Full Disk Access, a build running inside a Flatpak, a Windows process
    /// that is not elevated - so without this the code that turns them into
    /// something the person reads could only be exercised on three machines
    /// nobody has to hand.
    void putInTheWay(platform::AccessObstacle obstacle) {
        obstacles_.push_back(std::move(obstacle));
    }

    [[nodiscard]] QList<platform::StorageVolume> storageVolumes() const override {
        return volumes_;
    }

    // Everything else is whatever this machine says.
    [[nodiscard]] platform::EnvironmentInfo environment() const override {
        platform::EnvironmentInfo info = real_->environment();
        if (pretend_ != format::OsFamily::Unknown) {
            info.os = pretend_;
        }
        return info;
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
    [[nodiscard]] QList<platform::AccessObstacle> accessObstacles() const override {
        return obstacles_.isEmpty() ? real_->accessObstacles() : obstacles_;
    }

private:
    std::unique_ptr<platform::PlatformService> real_;
    format::OsFamily pretend_ = format::OsFamily::Unknown;
    QList<platform::AccessObstacle> obstacles_;
    QList<platform::StorageVolume> volumes_;
};

}  // namespace transmit::testing
