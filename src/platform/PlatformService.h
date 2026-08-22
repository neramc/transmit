#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <memory>

#include "format/PathToken.h"
#include "format/Result.h"

namespace transmit::platform {

using format::OsFamily;
using format::PathTokenMap;

/// What the capture records about the machine it ran on, and what the restore
/// side shows the user before touching anything.
struct EnvironmentInfo {
    OsFamily os = OsFamily::Unknown;
    QString osName;              ///< "Windows 11 Pro", "Ubuntu 24.04.1 LTS", "macOS 14.5"
    QString osVersion;
    QString distroId;            ///< /etc/os-release ID; empty off Linux
    QString distroLike;          ///< ID_LIKE, so derivatives map to their parent
    QString desktopEnvironment;  ///< GNOME, KDE, XFCE, ...; empty off Linux
    QString hostName;
    QString userName;
    QString homeDirectory;
    QString architecture;
};

/// A drive the user can write an archive to. Removable media is listed first
/// because that is the intended target, but any writable volume is allowed.
struct StorageVolume {
    QString displayName;
    QString rootPath;
    QString fileSystem;      ///< "vfat", "exfat", "NTFS", "apfs", ...
    quint64 totalBytes = 0;
    quint64 freeBytes = 0;
    bool removable = false;
    bool readOnly = false;

    /// FAT32 cannot hold a file of 4 GiB or more, so an archive bound for one
    /// has to be split.
    [[nodiscard]] bool requiresSplitting() const;

    /// The largest single file this volume accepts, or 0 when unlimited.
    [[nodiscard]] quint64 maximumFileSize() const;
};

/// Where an installed application came from, which decides how the restore
/// side proposes to reinstall it.
enum class PackageSource {
    Unknown,
    WindowsRegistry,
    Winget,
    Msix,
    MacBundle,
    Homebrew,
    MacAppStore,
    Apt,
    Dnf,
    Zypper,
    Pacman,
    Portage,
    Nix,
    Apk,
    Xbps,
    Slackware,
    Flatpak,
    Snap,
    AppImage,
};

QString packageSourceName(PackageSource source);

struct InstalledApp {
    QString id;           ///< package name, bundle id or registry key
    QString displayName;
    QString version;
    QString publisher;
    PackageSource source = PackageSource::Unknown;
    QString installLocation;
};

/// A process holding files the capture wants to read. Rather than forcing a
/// close, Transmit asks the user to quit these so the data is consistent.
struct RunningApp {
    QString processName;
    QString displayName;
    qint64 processId = 0;
};

/// A point-in-time view of the filesystem, so live databases can be copied
/// consistently. Falls back to reading the live files when the platform or the
/// user's privileges do not allow a real snapshot.
class Snapshot {
public:
    virtual ~Snapshot() = default;

    /// True when this is a real snapshot rather than the pass-through fallback.
    [[nodiscard]] virtual bool isRealSnapshot() const = 0;

    /// Maps a live path to its location inside the snapshot. Returns the input
    /// unchanged for the fallback.
    [[nodiscard]] virtual QString translate(const QString& livePath) const = 0;

    /// Why a real snapshot was not available, for the capture report.
    [[nodiscard]] virtual QString unavailableReason() const = 0;
};

/// The one seam where operating system differences live. Everything above this
/// interface is written once; see the windows/, macos/ and linux/ directories
/// for the implementations.
class PlatformService {
public:
    virtual ~PlatformService() = default;

    /// The implementation for the running OS.
    static std::unique_ptr<PlatformService> create();

    [[nodiscard]] virtual EnvironmentInfo environment() const = 0;

    /// The known-folder table for this machine, used to tokenise paths.
    [[nodiscard]] virtual PathTokenMap knownFolders() const = 0;

    [[nodiscard]] virtual QList<StorageVolume> storageVolumes() const = 0;

    /// Enumerates installed applications. Slow on some platforms (it may shell
    /// out to a package manager), so callers run it off the UI thread.
    [[nodiscard]] virtual QList<InstalledApp> installedApplications() const = 0;

    /// Which of the named processes are currently running.
    [[nodiscard]] virtual QList<RunningApp> runningApplications(
        const QStringList& processNames) const = 0;

    /// Takes a snapshot covering `paths`. Never fails: when a real snapshot is
    /// impossible it returns a pass-through that explains why.
    [[nodiscard]] virtual std::unique_ptr<Snapshot> createSnapshot(
        const QStringList& paths) const = 0;

    /// The command a generated script should use to install packages, for
    /// example "sudo apt install -y". Empty when unknown.
    [[nodiscard]] virtual QString packageInstallCommand() const = 0;

    /// Package manager native to this system, used to pick catalog entries.
    [[nodiscard]] virtual PackageSource nativePackageSource() const = 0;
};

}  // namespace transmit::platform
