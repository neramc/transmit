#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QList>
#include <QQmlEngine>
#include <QString>

#include <memory>

#include "core/continuity/ContinuityTypes.h"
#include "core/services/ScanService.h"
#include "platform/PlatformService.h"

namespace transmit::app {

/// The folders of your own that a capture can take, and how large each is.
///
/// A profile is a reasonable default and not an answer to "I want my documents
/// but not four hundred gigabytes of video". Until now that was the only
/// choice on offer: the domains could be turned off whole, and inside "your
/// files" it was everything or nothing. This is the list that makes the
/// question askable at the level people actually think about it - by folder,
/// with the size of each one next to it, because nobody can decide what to
/// leave behind without knowing what it costs.
///
/// The sizes come from one scan of all of them rather than a scan each: the
/// scanner already records which known folder every item belongs to, so
/// walking the tree once and adding up per folder gives the same answer for a
/// fraction of the work.
class CaptureFolderModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool measuring READ isMeasuring NOTIFY measuringChanged)
    Q_PROPERTY(bool measured READ isMeasured NOTIFY measuredChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(QString selectionSummary READ selectionSummary NOTIFY selectionChanged)

public:
    enum Roles {
        TokenNameRole = Qt::UserRole + 1,
        DisplayNameRole,
        PathRole,
        SizeBytesRole,
        SizeTextRole,
        FileCountRole,
        SelectedRole,
        PresentRole,  ///< the folder exists on this machine
    };
    Q_ENUM(Roles)

    explicit CaptureFolderModel(QObject* parent = nullptr);
    ~CaptureFolderModel() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool isMeasuring() const { return measuring_; }
    [[nodiscard]] bool isMeasured() const { return measured_; }
    [[nodiscard]] int selectedCount() const;

    /// "Documents, Pictures and two others - 4.1 GB", for the step's summary.
    [[nodiscard]] QString selectionSummary() const;

    /// Works out how large each folder is, on a worker.
    ///
    /// Walking a home directory is seconds at best and minutes on a machine
    /// with a lot on it, so this cannot happen on the thread that repaints.
    /// Calling it while a measurement is running does nothing.
    Q_INVOKABLE void measure();

    Q_INVOKABLE void setSelected(int row, bool selected);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void selectNone();

    /// The roots a capture should take, for the selection the user has made.
    /// Empty when everything is selected, which is what a profile means on its
    /// own - so an untouched list changes nothing.
    [[nodiscard]] QList<core::CaptureRoot> chosenRoots() const;

    /// Whether the user has narrowed the list at all.
    [[nodiscard]] bool isNarrowed() const;

signals:
    void measuringChanged();
    void measuredChanged();
    void selectionChanged();
    void countsChanged();

private:
    struct Folder {
        format::PathTokenId token = format::PathTokenId::Documents;
        QString displayName;
        QString path;
        bool present = false;
        bool selected = true;
        quint64 sizeBytes = 0;
        quint64 fileCount = 0;
    };

    void handleMeasured();

    std::unique_ptr<platform::PlatformService> platform_;
    QList<Folder> folders_;

    QFutureWatcher<QHash<int, QPair<quint64, quint64>>> watcher_;
    core::CancelToken cancelToken_;
    bool measuring_ = false;
    bool measured_ = false;
};

}  // namespace transmit::app
