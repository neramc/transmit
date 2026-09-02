#include "core/update/InstallKind.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

namespace transmit::core {
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
/// before anything else so a developer's copy is never treated as an install.
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

InstallKind detectInstallKind() {
    const QString path = programPath();
    if (path.isEmpty()) {
        return InstallKind::Unknown;
    }

    // Every sandbox and package manager first, so none of the shape-based
    // guesses below can talk over them.
    if (environmentHas("FLATPAK_ID") || QFileInfo::exists(QStringLiteral("/.flatpak-info")) ||
        environmentHas("SNAP") || environmentHas("APPDIR_PACKAGE")) {
        return InstallKind::PackageManaged;
    }

    if (insideBuildTree(path)) {
        return InstallKind::Development;
    }

#if defined(Q_OS_LINUX)
    // AppRun sets APPIMAGE to the AppImage's own path. Without it there is no
    // reliable way to find the file to replace, so a bundle started some other
    // way is not treated as one.
    const QString appImage = environmentValue("APPIMAGE");
    if (!appImage.isEmpty() && QFileInfo::exists(appImage)) {
        return InstallKind::AppImage;
    }

    // Anything under a system prefix belongs to whatever put it there.
    for (const QLatin1String prefix :
         {QLatin1String("/usr/"), QLatin1String("/opt/"), QLatin1String("/nix/store/"),
          QLatin1String("/snap/"), QLatin1String("/var/lib/flatpak/"), QLatin1String("/app/")}) {
        if (path.startsWith(prefix)) {
            return InstallKind::PackageManaged;
        }
    }
    return InstallKind::Unknown;

#elif defined(Q_OS_MACOS)
    if (path.contains(QLatin1String(".app/Contents/MacOS/"))) {
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
    return InstallKind::Unknown;

#elif defined(Q_OS_WIN)
    const QString programFiles = environmentValue("ProgramFiles");
    const QString programFilesX86 = environmentValue("ProgramFiles(x86)");
    for (const QString& prefix : {programFiles, programFilesX86}) {
        if (!prefix.isEmpty() &&
            path.startsWith(QDir::fromNativeSeparators(prefix), Qt::CaseInsensitive)) {
            return InstallKind::WindowsInstaller;
        }
    }
    return InstallKind::WindowsPortable;

#else
    return InstallKind::Unknown;
#endif
}

QString replaceableTarget(InstallKind kind) {
    if (!canReplaceItself(kind)) {
        return {};
    }
    const QString path = programPath();
    if (path.isEmpty()) {
        return {};
    }

    switch (kind) {
        case InstallKind::AppImage:
            return environmentValue("APPIMAGE");
        case InstallKind::MacBundle: {
            const qsizetype marker = path.indexOf(QLatin1String(".app/Contents/MacOS/"));
            return marker < 0 ? QString() : path.left(marker + 4);
        }
        case InstallKind::WindowsInstaller:
        case InstallKind::WindowsPortable:
            return QFileInfo(path).absolutePath();
        default:
            return {};
    }
}

}  // namespace transmit::core
