#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QList>
#include <QQmlEngine>
#include <QTimer>

#include <memory>

#include "platform/PlatformService.h"

namespace transmit::app {

/// The volumes a capture can be written to, refreshed periodically so a stick
/// inserted while the wizard is open appears without the user going back.
///
/// The enumeration itself runs on a worker thread. Asking the system what is
/// mounted means a statfs per mount point, and a stale network share or a
/// drive that has spun down will sit there for seconds - which, on a two
/// second timer on the interface thread, is a window that stops repainting.
class DriveListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int removableCount READ removableCount NOTIFY countsChanged)
    Q_PROPERTY(bool refreshing READ isRefreshing NOTIFY refreshingChanged)

public:
    enum Roles {
        DisplayNameRole = Qt::UserRole + 1,
        RootPathRole,
        FileSystemRole,
        TotalBytesRole,
        FreeBytesRole,
        FreeTextRole,
        TotalTextRole,
        RemovableRole,
        ReadOnlyRole,
        RequiresSplittingRole,
        SubtitleRole,
    };

    explicit DriveListModel(QObject* parent = nullptr);
    ~DriveListModel() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int removableCount() const;
    [[nodiscard]] bool isRefreshing() const { return refreshing_; }

    /// The volume at `row`, for callers that need the whole record.
    [[nodiscard]] platform::StorageVolume volumeAt(int row) const;

public slots:
    void refresh();

    /// Starts or stops the periodic refresh. The UI enables it only while a
    /// page that shows drives is visible.
    void setWatching(bool watching);

signals:
    void countsChanged();
    void refreshingChanged();

private:
    /// Takes the worker's answer and, if anything actually changed, publishes
    /// it. Runs on the interface thread.
    void applyScan();

    std::unique_ptr<platform::PlatformService> platform_;
    QFutureWatcher<QList<platform::StorageVolume>> watcher_;
    QList<platform::StorageVolume> volumes_;
    QTimer watchTimer_;
    bool refreshing_ = false;
};

}  // namespace transmit::app
