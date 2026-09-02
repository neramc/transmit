#pragma once

#include <QString>

namespace transmit::core {

/// How this copy of Transmit got onto the machine, which decides whether it is
/// allowed to replace itself.
enum class InstallKind {
    Unknown,           ///< could not tell: nothing is replaced
    AppImage,          ///< one file, replaceable in place
    WindowsInstaller,  ///< installed by the setup program
    WindowsPortable,   ///< unpacked from the zip
    MacBundle,         ///< a .app somebody dragged across
    PackageManaged,    ///< Flatpak, Snap, apt, rpm, Homebrew, a distribution
    Development,       ///< running out of a build tree
};

[[nodiscard]] QString describe(InstallKind kind);

/// Whether a copy installed this way may replace itself.
///
/// False for anything a package manager owns. Self-updating a packaged install
/// is not a preference to be overridden: the package manager's database would
/// still describe the old files, the next upgrade would overwrite whatever was
/// put there, and on Flatpak the sandbox has no network and no writable
/// program directory to begin with. Those installs are updated by the thing
/// that installed them.
[[nodiscard]] bool canReplaceItself(InstallKind kind);

/// Works out how this running program was installed. Answers Unknown rather
/// than guessing, because every wrong answer here ends in a file being
/// replaced that something else owns.
[[nodiscard]] InstallKind detectInstallKind();

/// The file that would be replaced by an update, or empty when nothing should
/// be. For an AppImage this is the AppImage; for a Windows install it is the
/// executable's directory; for a bundle it is the .app.
[[nodiscard]] QString replaceableTarget(InstallKind kind);

}  // namespace transmit::core
