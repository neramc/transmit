#include "platform/windows/WindowsPlatformService.h"

#include <QDir>
#include <QFileInfo>
#include <QHostInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"
#include "platform/windows/WindowsSecretStore.h"
#include "platform/windows/WindowsSettingsProvider.h"

#ifdef Q_OS_WIN
#include <tlhelp32.h>
#include <windows.h>
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
    return QString::fromLocal8Bit(process.readAllStandardOutput());
}

/// Walks one of the three uninstall hives. Both the 64- and 32-bit views are
/// read, because a 32-bit application on a 64-bit system registers under
/// WOW6432Node and would otherwise be missed.
void collectUninstallEntries(QList<InstalledApp>& apps, const QString& rootKey) {
    QSettings registry(rootKey, QSettings::NativeFormat);
    for (const QString& key : registry.childGroups()) {
        registry.beginGroup(key);
        const QString name = registry.value(QStringLiteral("DisplayName")).toString();
        const bool systemComponent =
            registry.value(QStringLiteral("SystemComponent"), 0).toInt() != 0;

        if (!name.isEmpty() && !systemComponent) {
            InstalledApp app;
            app.id = key;
            app.displayName = name;
            app.version = registry.value(QStringLiteral("DisplayVersion")).toString();
            app.publisher = registry.value(QStringLiteral("Publisher")).toString();
            app.installLocation = registry.value(QStringLiteral("InstallLocation")).toString();
            app.source = PackageSource::WindowsRegistry;
            apps.push_back(app);
        }
        registry.endGroup();
    }
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

EnvironmentInfo WindowsPlatformService::environment() const {
    EnvironmentInfo info;
    info.os = OsFamily::Windows;
    info.osName = QSysInfo::prettyProductName();
    info.osVersion = QSysInfo::productVersion();
    info.hostName = QHostInfo::localHostName();
    info.userName = qEnvironmentVariable("USERNAME");
    info.homeDirectory = QDir::homePath();
    info.architecture = QSysInfo::currentCpuArchitecture();
    return info;
}

PathTokenMap WindowsPlatformService::knownFolders() const {
    using format::PathTokenId;

    PathTokenMap map(OsFamily::Windows);
    const auto set = [&map](PathTokenId token, const QString& path) {
        if (!path.isEmpty()) {
            map.setBase(token, toUtf8(path));
        }
    };

    set(PathTokenId::Home, QDir::homePath());
    set(PathTokenId::Desktop, QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    set(PathTokenId::Documents,
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    set(PathTokenId::Downloads, QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    set(PathTokenId::Pictures, QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    set(PathTokenId::Music, QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
    set(PathTokenId::Videos, QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));

    // Roaming holds settings that follow a domain user between machines;
    // Local holds machine-specific state. Both matter for continuity, and they
    // map to different places on the other platforms.
    const QString roaming = qEnvironmentVariable("APPDATA");
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    set(PathTokenId::AppConfig,
        roaming.isEmpty() ? QDir::homePath() + QStringLiteral("/AppData/Roaming") : roaming);
    set(PathTokenId::AppData,
        local.isEmpty() ? QDir::homePath() + QStringLiteral("/AppData/Local") : local);
    set(PathTokenId::AppState,
        local.isEmpty() ? QDir::homePath() + QStringLiteral("/AppData/Local") : local);
    set(PathTokenId::Fonts,
        (local.isEmpty() ? QDir::homePath() + QStringLiteral("/AppData/Local") : local) +
            QStringLiteral("/Microsoft/Windows/Fonts"));

    const QString publicPath = qEnvironmentVariable("PUBLIC");
    set(PathTokenId::PublicShare,
        publicPath.isEmpty() ? QStringLiteral("C:/Users/Public") : publicPath);
    set(PathTokenId::Templates,
        (roaming.isEmpty() ? QDir::homePath() + QStringLiteral("/AppData/Roaming") : roaming) +
            QStringLiteral("/Microsoft/Windows/Templates"));
    return map;
}

QList<StorageVolume> WindowsPlatformService::storageVolumes() const {
    QList<StorageVolume> volumes;

    for (const QStorageInfo& info : QStorageInfo::mountedVolumes()) {
        if (!info.isValid() || !info.isReady()) {
            continue;
        }

        StorageVolume volume;
        volume.rootPath = info.rootPath();
        volume.displayName = info.displayName().isEmpty() ? info.rootPath() : info.displayName();
        volume.fileSystem = QString::fromUtf8(info.fileSystemType());
        volume.totalBytes = static_cast<quint64>(std::max<qint64>(info.bytesTotal(), 0));
        volume.freeBytes = static_cast<quint64>(std::max<qint64>(info.bytesAvailable(), 0));
        volume.readOnly = info.isReadOnly();

#ifdef Q_OS_WIN
        const std::wstring root = volume.rootPath.toStdWString();
        const UINT type = ::GetDriveTypeW(root.c_str());
        volume.removable = (type == DRIVE_REMOVABLE);
        if (type == DRIVE_CDROM) {
            continue;  // not a place to write an archive
        }
#endif
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

QList<InstalledApp> WindowsPlatformService::installedApplications() const {
    QList<InstalledApp> apps;

    collectUninstallEntries(
        apps, QStringLiteral(
                  "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"));
    collectUninstallEntries(
        apps, QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\"
                             "CurrentVersion\\Uninstall"));
    collectUninstallEntries(
        apps, QStringLiteral(
                  "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"));

    // winget gives stable package identifiers, which map far better onto other
    // platforms than a human-readable display name does.
    const QString winget =
        runCommand(QStringLiteral("winget"),
                   {QStringLiteral("list"), QStringLiteral("--disable-interactivity"),
                    QStringLiteral("--accept-source-agreements")});
    for (const QString& line : winget.split(u'\n', Qt::SkipEmptyParts)) {
        const QStringList columns = line.split(QStringLiteral("  "), Qt::SkipEmptyParts);
        if (columns.size() < 2) {
            continue;
        }
        const QString identifier = columns[1].trimmed();
        // A winget identifier always contains a publisher/package separator.
        if (!identifier.contains(u'.')) {
            continue;
        }
        InstalledApp app;
        app.id = identifier;
        app.displayName = columns[0].trimmed();
        app.version = columns.size() > 2 ? columns[2].trimmed() : QString();
        app.source = PackageSource::Winget;
        apps.push_back(app);
    }
    return apps;
}

QList<RunningApp> WindowsPlatformService::runningApplications(
    const QStringList& processNames) const {
    QList<RunningApp> running;
#ifdef Q_OS_WIN
    if (processNames.isEmpty()) {
        return running;
    }

    const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return running;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (::Process32FirstW(snapshot, &entry)) {
        do {
            const QString name = QString::fromWCharArray(entry.szExeFile);
            for (const QString& wanted : processNames) {
                const QString target = QFileInfo(wanted).fileName();
                if (name.compare(target, Qt::CaseInsensitive) == 0) {
                    running.push_back(
                        RunningApp{name, target, static_cast<qint64>(entry.th32ProcessID)});
                    break;
                }
            }
        } while (::Process32NextW(snapshot, &entry));
    }
    ::CloseHandle(snapshot);
#else
    Q_UNUSED(processNames);
#endif
    return running;
}

std::unique_ptr<Snapshot> WindowsPlatformService::createSnapshot(const QStringList& paths) const {
    Q_UNUSED(paths);
    // A Volume Shadow Copy needs administrator rights and a COM session; when
    // it is unavailable the capture still copies live databases consistently
    // through SQLite's online backup API.
    return std::make_unique<PassThroughSnapshot>(
        QObject::tr("A Volume Shadow Copy was not created. Live databases are still copied "
                    "consistently, but files written during the capture may be missed. Running "
                    "Transmit as an administrator enables shadow copies."));
}

QString WindowsPlatformService::packageInstallCommand() const {
    return QStringLiteral(
        "winget install --accept-package-agreements --accept-source-agreements -e --id");
}

PackageSource WindowsPlatformService::nativePackageSource() const {
    return PackageSource::Winget;
}

std::unique_ptr<SettingsProvider> WindowsPlatformService::settingsProvider() const {
    return std::make_unique<WindowsSettingsProvider>();
}

std::unique_ptr<SecretStore> WindowsPlatformService::secretStore() const {
    return std::make_unique<WindowsSecretStore>();
}

}  // namespace transmit::platform
