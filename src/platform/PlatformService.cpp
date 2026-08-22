#include "platform/PlatformService.h"

namespace transmit::platform {

QString packageSourceName(PackageSource source) {
    switch (source) {
        case PackageSource::WindowsRegistry:
            return QStringLiteral("registry");
        case PackageSource::Winget:
            return QStringLiteral("winget");
        case PackageSource::Msix:
            return QStringLiteral("msix");
        case PackageSource::MacBundle:
            return QStringLiteral("bundle");
        case PackageSource::Homebrew:
            return QStringLiteral("brew");
        case PackageSource::MacAppStore:
            return QStringLiteral("mas");
        case PackageSource::Apt:
            return QStringLiteral("apt");
        case PackageSource::Dnf:
            return QStringLiteral("dnf");
        case PackageSource::Zypper:
            return QStringLiteral("zypper");
        case PackageSource::Pacman:
            return QStringLiteral("pacman");
        case PackageSource::Portage:
            return QStringLiteral("emerge");
        case PackageSource::Nix:
            return QStringLiteral("nix");
        case PackageSource::Apk:
            return QStringLiteral("apk");
        case PackageSource::Xbps:
            return QStringLiteral("xbps");
        case PackageSource::Slackware:
            return QStringLiteral("slackpkg");
        case PackageSource::Flatpak:
            return QStringLiteral("flatpak");
        case PackageSource::Snap:
            return QStringLiteral("snap");
        case PackageSource::AppImage:
            return QStringLiteral("appimage");
        case PackageSource::Unknown:
            return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

quint64 StorageVolume::maximumFileSize() const {
    const QString type = fileSystem.toLower();
    // FAT32 stores the file size in 32 bits, so 4 GiB - 1 is the hard ceiling.
    if (type == QLatin1String("vfat") || type == QLatin1String("fat32") ||
        type == QLatin1String("msdos") || type == QLatin1String("fat")) {
        return 4ULL * 1024 * 1024 * 1024 - 1;
    }
    return 0;
}

bool StorageVolume::requiresSplitting() const {
    return maximumFileSize() != 0;
}

}  // namespace transmit::platform
