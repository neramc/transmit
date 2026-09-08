#pragma once

#include <QString>

#include "format/PathToken.h"

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

/// Everything the answer depends on, gathered in one place.
///
/// The rules are about a path and a handful of environment variables, and they
/// used to be asked of the machine the program was running on - so the Linux
/// rule could only be exercised on Linux, the Windows one on Windows, and each
/// of them only in whichever one of its cases that particular machine happened
/// to be in. This is the decision that says whether the updater may replace
/// the running program, and "nothing replaces a copy a package manager owns"
/// is a promise SECURITY.md makes, so being able to ask it on every system
/// about every system is the point.
struct InstallFacts {
    /// The running program's own file, absolute.
    QString programPath;

    /// Which system's rules to apply. Defaults to this one.
    format::OsFamily os = format::hostOsFamily();

    /// A CMake cache within four folders above the program.
    bool insideBuildTree = false;

    /// Flatpak, Snap or an AppDir package told us so outright.
    bool sandboxed = false;

    /// What AppRun put in APPIMAGE, and only when that file is really there:
    /// without it there is nothing to replace, so a bundle started some other
    /// way is not treated as one.
    QString appImagePath;

    /// Windows tells the program where its installed software lives rather
    /// than it being a fixed path, and on a non-English system it is not
    /// called "Program Files".
    QString programFiles;
    QString programFilesX86;
};

/// How a program with these facts about it was installed.
[[nodiscard]] InstallKind installKindFor(const InstallFacts& facts);

/// The same question asked of this running program: gathers the facts off the
/// machine and applies the rules. Answers Unknown rather than guessing,
/// because every wrong answer here ends in a file being replaced that
/// something else owns.
[[nodiscard]] InstallKind detectInstallKind();

/// The file that would be replaced by an update, or empty when nothing should
/// be. For an AppImage this is the AppImage; for a Windows install it is the
/// executable's directory; for a bundle it is the .app.
[[nodiscard]] QString targetFor(InstallKind kind, const InstallFacts& facts);

/// And for this running program.
[[nodiscard]] QString replaceableTarget(InstallKind kind);

}  // namespace transmit::core
