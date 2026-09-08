#pragma once

#include <optional>

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
    [[nodiscard]] std::unique_ptr<SecretStore> secretStore() const override;
    [[nodiscard]] QList<AccessObstacle> accessObstacles() const override;

protected:
    [[nodiscard]] QString unmountVolume(const QString& rootPath) const override;
};

/// Reads /etc/os-release into a key/value map. Exposed for testing and for the
/// application-catalog matcher, which needs ID and ID_LIKE.
QHash<QString, QString> readOsRelease(const QString& path = QStringLiteral("/etc/os-release"));

/// Maps an os-release ID (plus ID_LIKE) onto the distribution's package
/// manager. Derivatives fall back to their parent, so Mint resolves to apt and
/// Rocky to dnf without needing an entry of their own.
PackageSource packageSourceForDistro(const QString& id, const QString& idLike);

/// How to ask one package manager what is installed, and how to read what it
/// says back.
///
/// The asking and the reading were one function, which meant the reading could
/// only ever be checked on a machine that had that package manager - so eight
/// of the nine were checked nowhere. What a listing means is a rule about
/// text, and a rule about text can be given text.
struct PackageQuery {
    QString program;
    QStringList arguments;

    /// Applied to each line. Group 1 is the package name; group 2, where the
    /// listing has one, is the version.
    QString pattern;

    /// Listings that print a header row before the packages.
    int headerLines = 0;
};

/// How to ask this package manager, or nothing when Transmit does not know how
/// - which is the honest answer for Slackware, whose packages are a directory
/// rather than a command.
[[nodiscard]] std::optional<PackageQuery> packageQueryFor(PackageSource source);

/// The packages one listing's output describes.
///
/// A line that does not match the rule is not a package: listings carry
/// warnings, blank lines and continuation text, and inventing an entry from
/// one of those puts a package that does not exist into the install script.
[[nodiscard]] QList<InstalledApp> packagesFromListing(const QString& output, PackageSource source);

}  // namespace transmit::platform
