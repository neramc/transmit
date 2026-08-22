#include "platform/linux/LinuxPlatformService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QTextStream>

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"

namespace transmit::platform {
namespace {

using core::fromUtf8;
using core::toUtf8;

/// Runs a command and returns stdout, or an empty string when the tool is
/// missing or fails. Package managers are queried this way because none of
/// them offers a stable library interface.
QString runCommand(const QString& program, const QStringList& arguments, int timeoutMs = 20000) {
    if (QStandardPaths::findExecutable(program).isEmpty()) {
        return {};
    }

    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        qCWarning(logPlatform) << program << "did not finish in time";
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        qCDebug(logPlatform) << program << "exited with" << process.exitCode();
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput());
}

QString unquote(QString value) {
    value = value.trimmed();
    if (value.size() >= 2 && ((value.startsWith(u'"') && value.endsWith(u'"')) ||
                              (value.startsWith(u'\'') && value.endsWith(u'\'')))) {
        value = value.mid(1, value.size() - 2);
    }
    return value;
}

/// Reads ~/.config/user-dirs.dirs, which is where a localised desktop stores
/// the real names of Documents, Pictures and friends. Without this, a Korean
/// or German desktop would have its folders missed entirely.
QHash<QString, QString> readXdgUserDirs(const QString& home) {
    QHash<QString, QString> dirs;
    QFile file(home + QStringLiteral("/.config/user-dirs.dirs"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return dirs;
    }

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(u'#')) {
            continue;
        }
        const qsizetype separator = line.indexOf(u'=');
        if (separator <= 0) {
            continue;
        }
        const QString key = line.left(separator).trimmed();
        QString value = unquote(line.mid(separator + 1));
        value.replace(QStringLiteral("$HOME"), home);
        if (!value.isEmpty()) {
            dirs.insert(key, value);
        }
    }
    return dirs;
}

/// Strips a partition suffix so /dev/sda1 and /dev/nvme0n1p2 both resolve to
/// the parent device whose "removable" flag the kernel exposes.
QString parentBlockDevice(const QString& devicePath) {
    QString name = QFileInfo(devicePath).fileName();
    if (name.isEmpty()) {
        return {};
    }
    static const QRegularExpression nvme(QStringLiteral("^(nvme\\d+n\\d+)p\\d+$"));
    if (const auto match = nvme.match(name); match.hasMatch()) {
        return match.captured(1);
    }
    static const QRegularExpression mmc(QStringLiteral("^(mmcblk\\d+)p\\d+$"));
    if (const auto match = mmc.match(name); match.hasMatch()) {
        return match.captured(1);
    }
    static const QRegularExpression scsi(QStringLiteral("^([a-z]+)\\d+$"));
    if (const auto match = scsi.match(name); match.hasMatch()) {
        return match.captured(1);
    }
    return name;
}

bool isRemovableDevice(const QString& devicePath) {
    const QString parent = parentBlockDevice(devicePath);
    if (parent.isEmpty()) {
        return false;
    }

    QFile removable(QStringLiteral("/sys/block/%1/removable").arg(parent));
    if (removable.open(QIODevice::ReadOnly)) {
        if (removable.readAll().trimmed() == "1") {
            return true;
        }
    }

    // USB enclosures around a non-removable disk report removable=0, so the
    // transport is checked too. This is the common case for USB SSDs.
    const QFileInfo link(QStringLiteral("/sys/block/%1").arg(parent));
    const QString target = link.symLinkTarget();
    return target.contains(QStringLiteral("/usb"));
}

/// Crostini (the Linux container on ChromeOS) looks like Debian but mounts the
/// user's ChromeOS files under /mnt/chromeos, which capture must handle.
bool isChromiumOsContainer() {
    return QFile::exists(QStringLiteral("/dev/.cros_milestone")) ||
           QFile::exists(QStringLiteral("/etc/apt/sources.list.d/cros.list")) ||
           QDir(QStringLiteral("/mnt/chromeos")).exists();
}

void appendPackages(QList<InstalledApp>& apps, const QString& output, PackageSource source,
                    const QRegularExpression& pattern) {
    const QStringList lines = output.split(u'\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const auto match = pattern.match(line);
        if (!match.hasMatch()) {
            continue;
        }
        InstalledApp app;
        app.id = match.captured(1);
        app.displayName = app.id;
        app.version = match.lastCapturedIndex() >= 2 ? match.captured(2) : QString();
        app.source = source;
        apps.push_back(app);
    }
}

/// Pass-through "snapshot". Used when no volume-level snapshot is available;
/// the capture pipeline still copies live databases consistently through
/// SQLite's online backup API, so this is a degradation rather than a failure.
class PassThroughSnapshot final : public Snapshot {
public:
    explicit PassThroughSnapshot(QString reason) : reason_(std::move(reason)) {}

    [[nodiscard]] bool isRealSnapshot() const override { return false; }
    [[nodiscard]] QString translate(const QString& livePath) const override { return livePath; }
    [[nodiscard]] QString unavailableReason() const override { return reason_; }

private:
    QString reason_;
};

/// Btrfs read-only subvolume snapshot. Requires the btrfs tools and the
/// privileges to create a subvolume, so it is attempted and then abandoned
/// quietly rather than demanded.
class BtrfsSnapshot final : public Snapshot {
public:
    static std::unique_ptr<Snapshot> tryCreate(const QString& subvolume) {
        if (QStandardPaths::findExecutable(QStringLiteral("btrfs")).isEmpty()) {
            return nullptr;
        }
        const QString target =
            QStringLiteral("%1/.transmit-snapshot-%2").arg(subvolume).arg(QCoreApplication::applicationPid());

        QProcess process;
        process.start(QStringLiteral("btrfs"),
                      {QStringLiteral("subvolume"), QStringLiteral("snapshot"),
                       QStringLiteral("-r"), subvolume, target});
        if (!process.waitForFinished(30000) || process.exitCode() != 0) {
            qCDebug(logPlatform) << "btrfs snapshot unavailable:"
                                 << QString::fromUtf8(process.readAllStandardError()).trimmed();
            return nullptr;
        }
        return std::unique_ptr<Snapshot>(new BtrfsSnapshot(subvolume, target));
    }

    ~BtrfsSnapshot() override {
        QProcess process;
        process.start(QStringLiteral("btrfs"),
                      {QStringLiteral("subvolume"), QStringLiteral("delete"), snapshotPath_});
        process.waitForFinished(30000);
    }

    [[nodiscard]] bool isRealSnapshot() const override { return true; }

    [[nodiscard]] QString translate(const QString& livePath) const override {
        if (livePath.startsWith(subvolume_)) {
            return snapshotPath_ + livePath.mid(subvolume_.size());
        }
        return livePath;
    }

    [[nodiscard]] QString unavailableReason() const override { return {}; }

private:
    BtrfsSnapshot(QString subvolume, QString snapshotPath)
        : subvolume_(std::move(subvolume)), snapshotPath_(std::move(snapshotPath)) {}

    QString subvolume_;
    QString snapshotPath_;
};

}  // namespace

QHash<QString, QString> readOsRelease(const QString& path) {
    QHash<QString, QString> values;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // Some minimal systems only ship the usr-merged copy.
        QFile fallback(QStringLiteral("/usr/lib/os-release"));
        if (!fallback.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return values;
        }
        return readOsRelease(QStringLiteral("/usr/lib/os-release"));
    }

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(u'#')) {
            continue;
        }
        const qsizetype separator = line.indexOf(u'=');
        if (separator <= 0) {
            continue;
        }
        values.insert(line.left(separator).trimmed(), unquote(line.mid(separator + 1)));
    }
    return values;
}

PackageSource packageSourceForDistro(const QString& id, const QString& idLike) {
    // Checked most specific first; ID_LIKE catches derivatives that do not
    // appear here by name.
    const QStringList candidates = QStringList{id} + idLike.split(u' ', Qt::SkipEmptyParts);
    for (const QString& candidate : candidates) {
        const QString name = candidate.toLower();
        if (name == QLatin1String("debian") || name == QLatin1String("ubuntu") ||
            name == QLatin1String("linuxmint") || name == QLatin1String("pop") ||
            name == QLatin1String("raspbian") || name == QLatin1String("elementary")) {
            return PackageSource::Apt;
        }
        if (name == QLatin1String("fedora") || name == QLatin1String("rhel") ||
            name == QLatin1String("centos") || name == QLatin1String("rocky") ||
            name == QLatin1String("almalinux")) {
            return PackageSource::Dnf;
        }
        if (name.startsWith(QLatin1String("opensuse")) || name == QLatin1String("sles") ||
            name == QLatin1String("suse")) {
            return PackageSource::Zypper;
        }
        if (name == QLatin1String("arch") || name == QLatin1String("manjaro") ||
            name == QLatin1String("endeavouros") || name == QLatin1String("garuda")) {
            return PackageSource::Pacman;
        }
        if (name == QLatin1String("gentoo")) {
            return PackageSource::Portage;
        }
        if (name == QLatin1String("nixos")) {
            return PackageSource::Nix;
        }
        if (name == QLatin1String("alpine")) {
            return PackageSource::Apk;
        }
        if (name == QLatin1String("void")) {
            return PackageSource::Xbps;
        }
        if (name == QLatin1String("slackware")) {
            return PackageSource::Slackware;
        }
    }
    return PackageSource::Unknown;
}

EnvironmentInfo LinuxPlatformService::environment() const {
    const auto osRelease = readOsRelease();

    EnvironmentInfo info;
    info.os = OsFamily::Linux;
    info.distroId = osRelease.value(QStringLiteral("ID"));
    info.distroLike = osRelease.value(QStringLiteral("ID_LIKE"));
    info.osName = osRelease.value(QStringLiteral("PRETTY_NAME"),
                                  QSysInfo::prettyProductName());
    info.osVersion = osRelease.value(QStringLiteral("VERSION_ID"), QSysInfo::productVersion());
    info.hostName = QHostInfo::localHostName();
    info.userName = qEnvironmentVariable("USER", qEnvironmentVariable("LOGNAME"));
    info.homeDirectory = QDir::homePath();
    info.architecture = QSysInfo::currentCpuArchitecture();

    info.desktopEnvironment = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    if (info.desktopEnvironment.isEmpty()) {
        info.desktopEnvironment = qEnvironmentVariable("DESKTOP_SESSION");
    }

    if (isChromiumOsContainer()) {
        info.distroId = QStringLiteral("chromiumos");
        info.distroLike = QStringLiteral("debian");
        if (!info.osName.contains(QLatin1String("Chrome"), Qt::CaseInsensitive)) {
            info.osName = QStringLiteral("ChromeOS Linux (%1)").arg(info.osName);
        }
    }
    return info;
}

PathTokenMap LinuxPlatformService::knownFolders() const {
    using format::PathTokenId;

    const QString home = QDir::homePath();
    PathTokenMap map(OsFamily::Linux);
    map.setBase(PathTokenId::Home, toUtf8(home));

    // QStandardPaths already reads user-dirs.dirs, but reading it directly as
    // well covers desktops where the variables are set but the Qt platform
    // theme has not picked them up.
    const auto userDirs = readXdgUserDirs(home);
    const auto resolve = [&](QStandardPaths::StandardLocation location, const char* xdgKey,
                             const QString& fallback) {
        const QString fromXdg = userDirs.value(QString::fromLatin1(xdgKey));
        if (!fromXdg.isEmpty()) {
            return fromXdg;
        }
        const QString fromQt = QStandardPaths::writableLocation(location);
        return fromQt.isEmpty() ? fallback : fromQt;
    };

    map.setBase(PathTokenId::Desktop,
                toUtf8(resolve(QStandardPaths::DesktopLocation, "XDG_DESKTOP_DIR",
                               home + QStringLiteral("/Desktop"))));
    map.setBase(PathTokenId::Documents,
                toUtf8(resolve(QStandardPaths::DocumentsLocation, "XDG_DOCUMENTS_DIR",
                               home + QStringLiteral("/Documents"))));
    map.setBase(PathTokenId::Downloads,
                toUtf8(resolve(QStandardPaths::DownloadLocation, "XDG_DOWNLOAD_DIR",
                               home + QStringLiteral("/Downloads"))));
    map.setBase(PathTokenId::Pictures,
                toUtf8(resolve(QStandardPaths::PicturesLocation, "XDG_PICTURES_DIR",
                               home + QStringLiteral("/Pictures"))));
    map.setBase(PathTokenId::Music,
                toUtf8(resolve(QStandardPaths::MusicLocation, "XDG_MUSIC_DIR",
                               home + QStringLiteral("/Music"))));
    map.setBase(PathTokenId::Videos,
                toUtf8(resolve(QStandardPaths::MoviesLocation, "XDG_VIDEOS_DIR",
                               home + QStringLiteral("/Videos"))));
    map.setBase(PathTokenId::PublicShare,
                toUtf8(resolve(QStandardPaths::PublicShareLocation, "XDG_PUBLICSHARE_DIR",
                               home + QStringLiteral("/Public"))));
    map.setBase(PathTokenId::Templates,
                toUtf8(resolve(QStandardPaths::TemplatesLocation, "XDG_TEMPLATES_DIR",
                               home + QStringLiteral("/Templates"))));

    const QString configHome =
        qEnvironmentVariable("XDG_CONFIG_HOME", home + QStringLiteral("/.config"));
    const QString dataHome =
        qEnvironmentVariable("XDG_DATA_HOME", home + QStringLiteral("/.local/share"));
    const QString stateHome =
        qEnvironmentVariable("XDG_STATE_HOME", home + QStringLiteral("/.local/state"));

    map.setBase(PathTokenId::AppConfig, toUtf8(configHome));
    map.setBase(PathTokenId::AppData, toUtf8(dataHome));
    map.setBase(PathTokenId::AppState, toUtf8(stateHome));
    map.setBase(PathTokenId::Fonts, toUtf8(dataHome + QStringLiteral("/fonts")));
    return map;
}

QList<StorageVolume> LinuxPlatformService::storageVolumes() const {
    QList<StorageVolume> volumes;

    for (const QStorageInfo& info : QStorageInfo::mountedVolumes()) {
        if (!info.isValid() || !info.isReady()) {
            continue;
        }

        const QString root = info.rootPath();
        const QString device = QString::fromUtf8(info.device());

        // Kernel bookkeeping mounts are not places a user stores an archive.
        if (!device.startsWith(QLatin1String("/dev/"))) {
            continue;
        }
        if (root.startsWith(QLatin1String("/snap/")) ||
            root.startsWith(QLatin1String("/var/lib/docker"))) {
            continue;
        }

        // Read-only image mounts - snaps, AppImages, container layers - are
        // never somewhere a user keeps or writes an archive, and listing them
        // buries the drives that matter.
        const QString fileSystemType = QString::fromUtf8(info.fileSystemType());
        if (fileSystemType == QLatin1String("squashfs") ||
            fileSystemType == QLatin1String("overlay") ||
            fileSystemType == QLatin1String("erofs")) {
            continue;
        }

        StorageVolume volume;
        volume.rootPath = root;
        volume.displayName = info.displayName().isEmpty() ? root : info.displayName();
        volume.fileSystem = fileSystemType;
        volume.totalBytes = static_cast<quint64>(std::max<qint64>(info.bytesTotal(), 0));
        volume.freeBytes = static_cast<quint64>(std::max<qint64>(info.bytesAvailable(), 0));
        volume.readOnly = info.isReadOnly();
        volume.removable = isRemovableDevice(device);
        volumes.push_back(volume);
    }

    // Removable media first: that is what the user is looking for.
    std::sort(volumes.begin(), volumes.end(), [](const StorageVolume& a, const StorageVolume& b) {
        if (a.removable != b.removable) {
            return a.removable;
        }
        return a.rootPath < b.rootPath;
    });
    return volumes;
}

QList<InstalledApp> LinuxPlatformService::installedApplications() const {
    QList<InstalledApp> apps;
    const EnvironmentInfo info = environment();
    const PackageSource native = packageSourceForDistro(info.distroId, info.distroLike);

    switch (native) {
        case PackageSource::Apt: {
            const QString output = runCommand(
                QStringLiteral("dpkg-query"),
                {QStringLiteral("-W"), QStringLiteral("-f=${Package}\\t${Version}\\n")});
            appendPackages(apps, output, PackageSource::Apt,
                           QRegularExpression(QStringLiteral("^(\\S+)\\t(\\S*)$")));
            break;
        }
        case PackageSource::Dnf:
        case PackageSource::Zypper: {
            const QString output =
                runCommand(QStringLiteral("rpm"),
                           {QStringLiteral("-qa"), QStringLiteral("--qf"),
                            QStringLiteral("%{NAME}\\t%{VERSION}-%{RELEASE}\\n")});
            appendPackages(apps, output, native,
                           QRegularExpression(QStringLiteral("^(\\S+)\\t(\\S*)$")));
            break;
        }
        case PackageSource::Pacman: {
            const QString output = runCommand(QStringLiteral("pacman"), {QStringLiteral("-Qe")});
            appendPackages(apps, output, PackageSource::Pacman,
                           QRegularExpression(QStringLiteral("^(\\S+) (\\S+)$")));
            break;
        }
        case PackageSource::Portage: {
            const QString output =
                runCommand(QStringLiteral("qlist"), {QStringLiteral("-ICv")});
            appendPackages(apps, output, PackageSource::Portage,
                           QRegularExpression(QStringLiteral("^(\\S+)$")));
            break;
        }
        case PackageSource::Nix: {
            const QString output =
                runCommand(QStringLiteral("nix"), {QStringLiteral("profile"), QStringLiteral("list")});
            appendPackages(apps, output, PackageSource::Nix,
                           QRegularExpression(QStringLiteral("([^\\s#]+)$")));
            break;
        }
        case PackageSource::Apk: {
            const QString output = runCommand(QStringLiteral("apk"), {QStringLiteral("info")});
            appendPackages(apps, output, PackageSource::Apk,
                           QRegularExpression(QStringLiteral("^(\\S+)$")));
            break;
        }
        case PackageSource::Xbps: {
            const QString output =
                runCommand(QStringLiteral("xbps-query"), {QStringLiteral("-m")});
            appendPackages(apps, output, PackageSource::Xbps,
                           QRegularExpression(QStringLiteral("^(\\S+?)-([^-]+)$")));
            break;
        }
        case PackageSource::Slackware: {
            // Slackware has no package database beyond this directory listing.
            const QDir packages(QStringLiteral("/var/log/packages"));
            for (const QString& name : packages.entryList(QDir::Files)) {
                InstalledApp app;
                app.id = name;
                app.displayName = name;
                app.source = PackageSource::Slackware;
                apps.push_back(app);
            }
            break;
        }
        default:
            qCInfo(logPlatform) << "no native package manager recognised for distro"
                                << info.distroId;
            break;
    }

    // Cross-distribution formats sit alongside whatever the system uses.
    const QString flatpak =
        runCommand(QStringLiteral("flatpak"),
                   {QStringLiteral("list"), QStringLiteral("--app"),
                    QStringLiteral("--columns=application,version")});
    appendPackages(apps, flatpak, PackageSource::Flatpak,
                   QRegularExpression(QStringLiteral("^(\\S+)\\t?(\\S*)$")));

    const QString snap = runCommand(QStringLiteral("snap"), {QStringLiteral("list")});
    const QStringList snapLines = snap.split(u'\n', Qt::SkipEmptyParts);
    for (qsizetype i = 1; i < snapLines.size(); ++i) {  // skip the header row
        const QStringList columns = snapLines[i].split(u' ', Qt::SkipEmptyParts);
        if (columns.size() >= 2) {
            InstalledApp app;
            app.id = columns[0];
            app.displayName = columns[0];
            app.version = columns[1];
            app.source = PackageSource::Snap;
            apps.push_back(app);
        }
    }

    for (const QString& directory :
         {QDir::homePath() + QStringLiteral("/Applications"),
          QDir::homePath() + QStringLiteral("/.local/bin")}) {
        const QDir dir(directory);
        for (const QFileInfo& file : dir.entryInfoList(QStringList{QStringLiteral("*.AppImage")},
                                                       QDir::Files)) {
            InstalledApp app;
            app.id = file.completeBaseName();
            app.displayName = file.completeBaseName();
            app.source = PackageSource::AppImage;
            app.installLocation = file.absoluteFilePath();
            apps.push_back(app);
        }
    }
    return apps;
}

QList<RunningApp> LinuxPlatformService::runningApplications(
    const QStringList& processNames) const {
    QList<RunningApp> running;
    if (processNames.isEmpty()) {
        return running;
    }

    const QDir proc(QStringLiteral("/proc"));
    for (const QString& entry : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool isPid = false;
        const qint64 pid = entry.toLongLong(&isPid);
        if (!isPid) {
            continue;
        }

        QFile comm(QStringLiteral("/proc/%1/comm").arg(entry));
        if (!comm.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;  // the process exited between listing and reading
        }
        const QString name = QString::fromUtf8(comm.readAll()).trimmed();

        for (const QString& wanted : processNames) {
            // /proc/<pid>/comm is capped at 15 characters, so a long process
            // name is compared by prefix.
            const QString target = QFileInfo(wanted).fileName();
            if (name.compare(target, Qt::CaseInsensitive) == 0 ||
                (target.size() > 15 && target.startsWith(name, Qt::CaseInsensitive))) {
                running.push_back(RunningApp{name, target, pid});
                break;
            }
        }
    }
    return running;
}

std::unique_ptr<Snapshot> LinuxPlatformService::createSnapshot(const QStringList& paths) const {
    if (paths.isEmpty()) {
        return std::make_unique<PassThroughSnapshot>(QString());
    }

    const QStorageInfo storage(paths.first());
    const QString type = QString::fromUtf8(storage.fileSystemType());

    if (type == QLatin1String("btrfs")) {
        if (auto snapshot = BtrfsSnapshot::tryCreate(storage.rootPath())) {
            qCInfo(logPlatform) << "using a btrfs snapshot of" << storage.rootPath();
            return snapshot;
        }
        return std::make_unique<PassThroughSnapshot>(QObject::tr(
            "A Btrfs snapshot could not be created (this usually needs administrator rights). "
            "Live databases are still copied consistently."));
    }

    return std::make_unique<PassThroughSnapshot>(
        QObject::tr("Filesystem snapshots are not available on %1. Live databases are still "
                    "copied consistently, but files written during the capture may be missed.")
            .arg(type.isEmpty() ? QStringLiteral("this filesystem") : type));
}

QString LinuxPlatformService::packageInstallCommand() const {
    const EnvironmentInfo info = environment();
    switch (packageSourceForDistro(info.distroId, info.distroLike)) {
        case PackageSource::Apt:       return QStringLiteral("sudo apt install -y");
        case PackageSource::Dnf:       return QStringLiteral("sudo dnf install -y");
        case PackageSource::Zypper:    return QStringLiteral("sudo zypper install -y");
        case PackageSource::Pacman:    return QStringLiteral("sudo pacman -S --needed --noconfirm");
        case PackageSource::Portage:   return QStringLiteral("sudo emerge --ask=n");
        case PackageSource::Nix:       return QStringLiteral("nix profile install nixpkgs#");
        case PackageSource::Apk:       return QStringLiteral("sudo apk add");
        case PackageSource::Xbps:      return QStringLiteral("sudo xbps-install -y");
        case PackageSource::Slackware: return QStringLiteral("sudo slackpkg install");
        default:                       return {};
    }
}

PackageSource LinuxPlatformService::nativePackageSource() const {
    const EnvironmentInfo info = environment();
    return packageSourceForDistro(info.distroId, info.distroLike);
}

}  // namespace transmit::platform
