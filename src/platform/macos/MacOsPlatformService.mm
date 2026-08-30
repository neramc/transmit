#include "platform/macos/MacOsPlatformService.h"

#include "platform/macos/MacOsSecretStore.h"
#include "platform/macos/MacOsSettingsProvider.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"

#ifdef Q_OS_MACOS
#import <Foundation/Foundation.h>
#endif

namespace transmit::platform {
namespace {

using core::toUtf8;

QString runCommand(const QString& program, const QStringList& arguments, int timeoutMs = 30000) {
    if (QStandardPaths::findExecutable(program).isEmpty()) {
        return {};
    }
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        return {};
    }
    if (process.exitCode() != 0) {
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput());
}

/// Reads an application bundle's Info.plist for its identifier and version.
/// The bundle identifier is what the catalog matches on, because it is stable
/// across releases and localisations in a way the display name is not.
InstalledApp readBundle(const QFileInfo& bundle) {
    InstalledApp app;
    app.displayName = bundle.completeBaseName();
    app.installLocation = bundle.absoluteFilePath();
    app.source = PackageSource::MacBundle;

#ifdef Q_OS_MACOS
    @autoreleasepool {
        NSString* path = bundle.absoluteFilePath().toNSString();
        NSBundle* nsBundle = [NSBundle bundleWithPath:path];
        if (nsBundle != nil) {
            NSString* identifier = [nsBundle bundleIdentifier];
            if (identifier != nil) {
                app.id = QString::fromNSString(identifier);
            }
            NSString* version =
                [[nsBundle infoDictionary] objectForKey:@"CFBundleShortVersionString"];
            if (version != nil) {
                app.version = QString::fromNSString(version);
            }
            NSString* name = [[nsBundle infoDictionary] objectForKey:@"CFBundleName"];
            if (name != nil) {
                app.displayName = QString::fromNSString(name);
            }
        }
    }
#endif

    if (app.id.isEmpty()) {
        app.id = bundle.completeBaseName();
    }
    return app;
}

class PassThroughSnapshot final : public Snapshot {
public:
    explicit PassThroughSnapshot(QString reason) : reason_(std::move(reason)) {}
    [[nodiscard]] bool isRealSnapshot() const override { return false; }
    [[nodiscard]] QString translate(const QString& livePath) const override { return livePath; }
    [[nodiscard]] QString unavailableReason() const override { return reason_; }

private:
    QString reason_;
};

}  // namespace

bool MacOsPlatformService::hasFullDiskAccess() {
    // There is no API that answers this directly; the accepted approach is to
    // attempt a read of a location the control protects.
    const QString probe =
        QDir::homePath() + QStringLiteral("/Library/Application Support/com.apple.TCC/TCC.db");
    QFile file(probe);
    if (file.open(QIODevice::ReadOnly)) {
        return true;
    }
    // Fall back to a second protected location so a missing probe file does not
    // read as "denied".
    QDir mail(QDir::homePath() + QStringLiteral("/Library/Mail"));
    return mail.exists() && !mail.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
}

EnvironmentInfo MacOsPlatformService::environment() const {
    EnvironmentInfo info;
    info.os = OsFamily::MacOs;
    info.osName = QSysInfo::prettyProductName();
    info.osVersion = QSysInfo::productVersion();
    info.hostName = QHostInfo::localHostName();
    info.userName = qEnvironmentVariable("USER");
    info.homeDirectory = QDir::homePath();
    info.architecture = QSysInfo::currentCpuArchitecture();
    return info;
}

PathTokenMap MacOsPlatformService::knownFolders() const {
    using format::PathTokenId;

    const QString home = QDir::homePath();
    PathTokenMap map(OsFamily::MacOs);
    const auto set = [&map](PathTokenId token, const QString& path) {
        if (!path.isEmpty()) {
            map.setBase(token, toUtf8(path));
        }
    };

    // Every folder below hangs off the same home directory.
    //
    // QStandardPaths would answer for the user folders, but on macOS it asks
    // Cocoa, which reports the account's real home and takes no notice of
    // $HOME. The rest of this table uses QDir::homePath, which does. The two
    // agree for an ordinary login and disagree the moment anything redirects
    // HOME - a service account, a sandbox, a test - and the table would then
    // describe two different machines at once, with {HOME} pointing one way
    // and {DOCUMENTS} the other.
    //
    // The names are fixed by the operating system, so deriving them loses
    // nothing.
    const auto userFolder = [&home](const char* name) {
        return home + QLatin1Char('/') + QLatin1String(name);
    };

    set(PathTokenId::Home, home);
    set(PathTokenId::Desktop, userFolder("Desktop"));
    set(PathTokenId::Documents, userFolder("Documents"));
    set(PathTokenId::Downloads, userFolder("Downloads"));
    set(PathTokenId::Pictures, userFolder("Pictures"));
    set(PathTokenId::Music, userFolder("Music"));
    set(PathTokenId::Videos, userFolder("Movies"));

    // macOS keeps both roaming-style and machine-local application data in
    // Application Support, so both tokens resolve there.
    set(PathTokenId::AppConfig, home + QStringLiteral("/Library/Application Support"));
    set(PathTokenId::AppData, home + QStringLiteral("/Library/Application Support"));
    set(PathTokenId::AppState, home + QStringLiteral("/Library/Saved Application State"));
    set(PathTokenId::Fonts, home + QStringLiteral("/Library/Fonts"));
    set(PathTokenId::PublicShare, home + QStringLiteral("/Public"));
    set(PathTokenId::Templates, home + QStringLiteral("/Templates"));
    return map;
}

QList<StorageVolume> MacOsPlatformService::storageVolumes() const {
    QList<StorageVolume> volumes;

    for (const QStorageInfo& info : QStorageInfo::mountedVolumes()) {
        if (!info.isValid() || !info.isReady()) {
            continue;
        }
        const QString root = info.rootPath();
        // System volumes and the read-only signed system snapshot are noise.
        if (root.startsWith(QLatin1String("/System/")) ||
            root.startsWith(QLatin1String("/private/var/vm"))) {
            continue;
        }

        StorageVolume volume;
        volume.rootPath = root;
        volume.displayName = info.displayName().isEmpty() ? root : info.displayName();
        volume.fileSystem = QString::fromUtf8(info.fileSystemType());
        volume.totalBytes = static_cast<quint64>(std::max<qint64>(info.bytesTotal(), 0));
        volume.freeBytes = static_cast<quint64>(std::max<qint64>(info.bytesAvailable(), 0));
        volume.readOnly = info.isReadOnly();
        // Everything the user mounts appears under /Volumes; the boot volume
        // is "/".
        volume.removable = root.startsWith(QLatin1String("/Volumes/"));
        volumes.push_back(volume);
    }

    std::sort(volumes.begin(), volumes.end(), [](const StorageVolume& a, const StorageVolume& b) {
        if (a.removable != b.removable) {
            return a.removable;
        }
        return a.rootPath < b.rootPath;
    });
    return volumes;
}

QList<InstalledApp> MacOsPlatformService::installedApplications() const {
    QList<InstalledApp> apps;

    for (const QString& directory :
         {QStringLiteral("/Applications"), QDir::homePath() + QStringLiteral("/Applications"),
          QStringLiteral("/Applications/Utilities")}) {
        const QDir dir(directory);
        for (const QFileInfo& bundle : dir.entryInfoList(QStringList{QStringLiteral("*.app")},
                                                         QDir::Dirs | QDir::NoDotAndDotDot)) {
            apps.push_back(readBundle(bundle));
        }
    }

    const QString brew =
        runCommand(QStringLiteral("brew"), {QStringLiteral("list"), QStringLiteral("--versions")});
    for (const QString& line : brew.split(u'\n', Qt::SkipEmptyParts)) {
        const QStringList columns = line.split(u' ', Qt::SkipEmptyParts);
        if (columns.isEmpty()) {
            continue;
        }
        InstalledApp app;
        app.id = columns.first();
        app.displayName = columns.first();
        app.version = columns.size() > 1 ? columns[1] : QString();
        app.source = PackageSource::Homebrew;
        apps.push_back(app);
    }

    const QString mas = runCommand(QStringLiteral("mas"), {QStringLiteral("list")});
    for (const QString& line : mas.split(u'\n', Qt::SkipEmptyParts)) {
        const QStringList columns = line.split(u' ', Qt::SkipEmptyParts);
        if (columns.size() < 2) {
            continue;
        }
        InstalledApp app;
        app.id = columns.first();
        app.displayName = columns.mid(1).join(u' ');
        app.source = PackageSource::MacAppStore;
        apps.push_back(app);
    }
    return apps;
}

QList<RunningApp> MacOsPlatformService::runningApplications(const QStringList& processNames) const {
    QList<RunningApp> running;
    if (processNames.isEmpty()) {
        return running;
    }

    const QString output =
        runCommand(QStringLiteral("ps"),
                   {QStringLiteral("-Ac"), QStringLiteral("-o"), QStringLiteral("pid=,comm=")});
    for (const QString& line : output.split(u'\n', Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        const qsizetype space = trimmed.indexOf(u' ');
        if (space <= 0) {
            continue;
        }
        const qint64 pid = trimmed.left(space).toLongLong();
        const QString name = trimmed.mid(space + 1).trimmed();

        for (const QString& wanted : processNames) {
            const QString target = QFileInfo(wanted).fileName();
            if (name.compare(target, Qt::CaseInsensitive) == 0) {
                running.push_back(RunningApp{name, target, pid});
                break;
            }
        }
    }
    return running;
}

std::unique_ptr<Snapshot> MacOsPlatformService::createSnapshot(const QStringList& paths) const {
    Q_UNUSED(paths);
    // An APFS local snapshot can be created with tmutil, but mounting it needs
    // elevated rights, so the capture relies on consistent database copies
    // instead and says so.
    return std::make_unique<PassThroughSnapshot>(
        QObject::tr("An APFS snapshot was not created. Live databases are still copied "
                    "consistently, but files written during the capture may be missed."));
}

QString MacOsPlatformService::packageInstallCommand() const {
    return QStringLiteral("brew install");
}

PackageSource MacOsPlatformService::nativePackageSource() const { return PackageSource::Homebrew; }

std::unique_ptr<SettingsProvider> MacOsPlatformService::settingsProvider() const {
    return std::make_unique<MacOsSettingsProvider>();
}

std::unique_ptr<SecretStore> MacOsPlatformService::secretStore() const {
    return std::make_unique<MacOsSecretStore>();
}

QString MacOsPlatformService::unmountVolume(const QString& rootPath) const {
    // diskutil is the supported way, takes the mount point directly, and does
    // the same thing dragging the drive to the bin does: unmount, then tell
    // the hardware it can spin down.
    QProcess process;
    process.setProgram(QStringLiteral("/usr/sbin/diskutil"));
    process.setArguments({QStringLiteral("eject"), rootPath});
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();

    if (!process.waitForFinished(20000)) {
        process.kill();
        process.waitForFinished(2000);
        return QCoreApplication::translate("Platform",
                                           "Ejecting %1 did not finish. Something is still using "
                                           "it; close it and try again.")
            .arg(QDir::toNativeSeparators(rootPath));
    }
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        return {};
    }

    QString reason = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (reason.isEmpty()) {
        reason = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    }
    return QCoreApplication::translate("Platform", "Could not eject %1: %2")
        .arg(QDir::toNativeSeparators(rootPath), reason);
}

}  // namespace transmit::platform
