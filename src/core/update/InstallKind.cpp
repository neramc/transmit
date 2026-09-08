#include "core/update/InstallKind.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

namespace transmit::core {

using format::OsFamily;

namespace {

QString programPath() {
    const QString path = QCoreApplication::applicationFilePath();
    return path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
}

bool environmentHas(const char* name) {
    return QProcessEnvironment::systemEnvironment().contains(QString::fromLatin1(name));
}

QString environmentValue(const char* name) {
    return QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(name));
}

/// A build tree, recognised by the CMake cache CMake leaves in it. Checked
/// before any shape-based guess so a developer's copy is never treated as an
/// install.
bool insideBuildTree(const QString& path) {
    QDir directory = QFileInfo(path).absoluteDir();
    for (int levels = 0; levels < 4 && !directory.isRoot(); ++levels) {
        if (directory.exists(QStringLiteral("CMakeCache.txt"))) {
            return true;
        }
        if (!directory.cdUp()) {
            break;
        }
    }
    return false;
}

/// The same path with the separators the rules are written in.
///
/// QDir::fromNativeSeparators does this only when the program is running on
/// Windows, which is right for a path off this machine and wrong inside a rule
/// that takes the system as a parameter: asked about a Windows path from
/// anywhere else it hands back the backslashes and the comparison below fails.
QString withForwardSlashes(QString path) {
    return path.replace(u'\\', u'/');
}

/// The folder a path is in, by the text.
///
/// QFileInfo::absolutePath answers about this machine's filesystem, so given
/// "C:/Program Files/Transmit/transmit.exe" on anything but Windows it decides
/// the path is relative and puts the working directory in front of it. What is
/// wanted here is the part before the last separator, which is the same answer
/// on the system the path came from.
QString folderOf(const QString& path) {
    const QString text = withForwardSlashes(path);
    const qsizetype separator = text.lastIndexOf(u'/');
    if (separator < 0) {
        return {};
    }
    return separator == 0 ? QStringLiteral("/") : text.left(separator);
}

/// Everything about this running program, read off the machine.
InstallFacts thisProgram() {
    InstallFacts facts;
    facts.programPath = programPath();
    facts.insideBuildTree = !facts.programPath.isEmpty() && insideBuildTree(facts.programPath);
    facts.sandboxed = environmentHas("FLATPAK_ID") ||
                      QFileInfo::exists(QStringLiteral("/.flatpak-info")) ||
                      environmentHas("SNAP") || environmentHas("APPDIR_PACKAGE");

    // AppRun sets APPIMAGE to the AppImage's own path. Without a file there is
    // nothing to replace, so the fact is not recorded unless it is really there.
    const QString appImage = environmentValue("APPIMAGE");
    if (!appImage.isEmpty() && QFileInfo::exists(appImage)) {
        facts.appImagePath = appImage;
    }

    facts.programFiles = environmentValue("ProgramFiles");
    facts.programFilesX86 = environmentValue("ProgramFiles(x86)");
    return facts;
}

}  // namespace

QString describe(InstallKind kind) {
    switch (kind) {
        case InstallKind::Unknown:
            return QStringLiteral("an installation of an unrecognised shape");
        case InstallKind::AppImage:
            return QStringLiteral("an AppImage");
        case InstallKind::WindowsInstaller:
            return QStringLiteral("an installed copy");
        case InstallKind::WindowsPortable:
            return QStringLiteral("a portable copy");
        case InstallKind::MacBundle:
            return QStringLiteral("an application bundle");
        case InstallKind::PackageManaged:
            return QStringLiteral("a copy your package manager installed");
        case InstallKind::Development:
            return QStringLiteral("a build tree");
    }
    return QStringLiteral("an installation of an unrecognised shape");
}

bool canReplaceItself(InstallKind kind) {
    switch (kind) {
        case InstallKind::AppImage:
        case InstallKind::WindowsInstaller:
        case InstallKind::WindowsPortable:
        case InstallKind::MacBundle:
            return true;
        case InstallKind::Unknown:
        case InstallKind::PackageManaged:
        case InstallKind::Development:
            return false;
    }
    return false;
}

InstallKind installKindFor(const InstallFacts& facts) {
    const QString& path = facts.programPath;
    if (path.isEmpty()) {
        return InstallKind::Unknown;
    }

    // Every sandbox and package manager first, so none of the shape-based
    // guesses below can talk over them.
    if (facts.sandboxed) {
        return InstallKind::PackageManaged;
    }

    if (facts.insideBuildTree) {
        return InstallKind::Development;
    }

    switch (facts.os) {
        case OsFamily::Linux: {
            if (!facts.appImagePath.isEmpty()) {
                return InstallKind::AppImage;
            }

            // Anything under a system prefix belongs to whatever put it there.
            for (const QLatin1String prefix :
                 {QLatin1String("/usr/"), QLatin1String("/opt/"), QLatin1String("/nix/store/"),
                  QLatin1String("/snap/"), QLatin1String("/var/lib/flatpak/"),
                  QLatin1String("/app/")}) {
                if (path.startsWith(prefix)) {
                    return InstallKind::PackageManaged;
                }
            }
            return InstallKind::Unknown;
        }

        case OsFamily::MacOs: {
            if (!path.contains(QLatin1String(".app/Contents/MacOS/"))) {
                return InstallKind::Unknown;
            }
            // Homebrew casks and MacPorts put bundles in their own prefixes and
            // track what they installed.
            for (const QLatin1String prefix :
                 {QLatin1String("/opt/homebrew/"), QLatin1String("/usr/local/Caskroom/"),
                  QLatin1String("/opt/local/")}) {
                if (path.startsWith(prefix)) {
                    return InstallKind::PackageManaged;
                }
            }
            return InstallKind::MacBundle;
        }

        case OsFamily::Windows: {
            const QString here = withForwardSlashes(path);
            for (const QString& prefix : {facts.programFiles, facts.programFilesX86}) {
                if (!prefix.isEmpty() &&
                    here.startsWith(withForwardSlashes(prefix), Qt::CaseInsensitive)) {
                    return InstallKind::WindowsInstaller;
                }
            }
            return InstallKind::WindowsPortable;
        }

        case OsFamily::Unknown:
            break;
    }
    return InstallKind::Unknown;
}

InstallKind detectInstallKind() {
    return installKindFor(thisProgram());
}

QString targetFor(InstallKind kind, const InstallFacts& facts) {
    if (!canReplaceItself(kind) || facts.programPath.isEmpty()) {
        return {};
    }

    switch (kind) {
        case InstallKind::AppImage:
            return facts.appImagePath;
        case InstallKind::MacBundle: {
            const qsizetype marker =
                facts.programPath.indexOf(QLatin1String(".app/Contents/MacOS/"));
            return marker < 0 ? QString() : facts.programPath.left(marker + 4);
        }
        case InstallKind::WindowsInstaller:
        case InstallKind::WindowsPortable:
            return folderOf(facts.programPath);
        default:
            return {};
    }
}

QString replaceableTarget(InstallKind kind) {
    return targetFor(kind, thisProgram());
}

}  // namespace transmit::core
