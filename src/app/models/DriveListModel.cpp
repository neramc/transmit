#include "app/models/DriveListModel.h"

#include <QtConcurrent/QtConcurrentRun>

#include "core/utils/Conversions.h"

using transmit::core::formatBytes;
using transmit::core::fromUtf8;

namespace transmit::app {
namespace {

/// Slow enough not to spin up idle disks, quick enough that inserting a stick
/// feels like it registered immediately.
constexpr int kWatchIntervalMs = 2000;

}  // namespace

DriveListModel::DriveListModel(QObject* parent) : QAbstractListModel(parent) {
    platform_ = platform::PlatformService::create();

    watchTimer_.setInterval(kWatchIntervalMs);
    connect(&watchTimer_, &QTimer::timeout, this, &DriveListModel::refresh);
    connect(&watcher_, &QFutureWatcher<QList<platform::StorageVolume>>::finished, this,
            &DriveListModel::applyScan);
    refresh();
}

DriveListModel::~DriveListModel() {
    // The worker holds a bare pointer to platform_, which is about to go.
    watcher_.waitForFinished();
}

int DriveListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(volumes_.size());
}

QVariant DriveListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= volumes_.size()) {
        return {};
    }
    const platform::StorageVolume& volume = volumes_.at(index.row());

    switch (role) {
        case DisplayNameRole:
            return volume.displayName;
        case RootPathRole:
            return volume.rootPath;
        case FileSystemRole:
            return volume.fileSystem;
        case TotalBytesRole:
            return volume.totalBytes;
        case FreeBytesRole:
            return volume.freeBytes;
        case FreeTextRole:
            return formatBytes(volume.freeBytes);
        case TotalTextRole:
            return formatBytes(volume.totalBytes);
        case RemovableRole:
            return volume.removable;
        case ReadOnlyRole:
            return volume.readOnly;
        case RequiresSplittingRole:
            return volume.requiresSplitting();
        case SubtitleRole: {
            QString subtitle =
                tr("%1 free of %2")
                    .arg(formatBytes(volume.freeBytes), formatBytes(volume.totalBytes));
            if (!volume.fileSystem.isEmpty()) {
                subtitle += QStringLiteral(" - ") + volume.fileSystem;
            }
            if (volume.readOnly) {
                subtitle += tr(" - read-only");
            } else if (volume.requiresSplitting()) {
                // Worth saying plainly: this is why the archive arrives in parts.
                subtitle +=
                    tr(" - this drive cannot hold a file over 4 GB, so the archive will "
                       "be written in parts");
            }
            return subtitle;
        }
        default:
            return {};
    }
}

QHash<int, QByteArray> DriveListModel::roleNames() const {
    return {{DisplayNameRole, "displayName"}, {RootPathRole, "rootPath"},
            {FileSystemRole, "fileSystem"},   {TotalBytesRole, "totalBytes"},
            {FreeBytesRole, "freeBytes"},     {FreeTextRole, "freeText"},
            {TotalTextRole, "totalText"},     {RemovableRole, "removable"},
            {ReadOnlyRole, "readOnly"},       {RequiresSplittingRole, "requiresSplitting"},
            {SubtitleRole, "subtitle"}};
}

int DriveListModel::removableCount() const {
    int count = 0;
    for (const platform::StorageVolume& volume : volumes_) {
        if (volume.removable) {
            ++count;
        }
    }
    return count;
}

platform::StorageVolume DriveListModel::volumeAt(int row) const {
    if (row < 0 || row >= volumes_.size()) {
        return {};
    }
    return volumes_.at(row);
}

void DriveListModel::refresh() {
    // One scan at a time. If a drive is being slow the timer will keep firing,
    // and queueing those up would only make the backlog worse.
    if (refreshing_) {
        return;
    }
    refreshing_ = true;
    emit refreshingChanged();

    platform::PlatformService* const platform = platform_.get();
    watcher_.setFuture(QtConcurrent::run([platform]() { return platform->storageVolumes(); }));
}

void DriveListModel::applyScan() {
    QList<platform::StorageVolume> updated = watcher_.result();
    refreshing_ = false;
    emit refreshingChanged();

    // Rebuilding the model on every tick would drop the user's selection, so
    // an unchanged list is left completely alone.
    const bool same =
        updated.size() == volumes_.size() &&
        std::equal(updated.begin(), updated.end(), volumes_.begin(),
                   [](const platform::StorageVolume& a, const platform::StorageVolume& b) {
                       return a.rootPath == b.rootPath && a.freeBytes == b.freeBytes &&
                              a.removable == b.removable;
                   });
    if (same) {
        return;
    }

    beginResetModel();
    volumes_ = std::move(updated);
    endResetModel();
    emit countsChanged();
}

void DriveListModel::setWatching(bool watching) {
    if (watching) {
        refresh();
        watchTimer_.start();
    } else {
        watchTimer_.stop();
    }
}

}  // namespace transmit::app
