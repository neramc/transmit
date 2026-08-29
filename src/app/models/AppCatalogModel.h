#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QQmlEngine>
#include <QString>

#include <memory>

#include "core/recipe/AppRecipe.h"
#include "platform/PlatformService.h"

namespace transmit::app {

/// The applications on this machine, and how much of each can come with you.
///
/// The distinction the interface has to make is between "its settings and data
/// travel" and "Transmit can note that you had it, and the restore will offer
/// to install it again". Both are worth having and they are not the same
/// promise, so a row says which it is rather than leaving somebody to find out
/// afterwards.
///
/// Detection runs on a worker thread: asking the system what is installed
/// means reading a package database, and on Windows walking the registry -
/// seconds, on the thread that is supposed to be repainting.
class AppCatalogModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countsChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countsChanged)
    Q_PROPERTY(int carriesDataCount READ carriesDataCount NOTIFY countsChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
    Q_PROPERTY(
        bool carriesDataOnly READ carriesDataOnly WRITE setCarriesDataOnly NOTIFY filterChanged)

public:
    enum Roles {
        AppIdRole = Qt::UserRole + 1,
        DisplayNameRole,
        CarriesDataRole,   ///< its own data can travel, and is here to be read
        HasStateRole,      ///< at least one of its folders exists on this machine
        InstalledRole,     ///< the package database knows about it
        StateSummaryRole,  ///< "profile, config" - which roots it has here
        GradeRole,
        GradeNameRole,
        NoteRole,
        SelectedRole,
    };

    explicit AppCatalogModel(QObject* parent = nullptr);
    ~AppCatalogModel() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool isLoading() const { return loading_; }

    /// Every application found, whatever the filter is hiding.
    [[nodiscard]] int totalCount() const { return static_cast<int>(rows_.size()); }
    [[nodiscard]] int carriesDataCount() const;
    [[nodiscard]] int selectedCount() const;

    [[nodiscard]] QString filterText() const { return filterText_; }
    void setFilterText(const QString& text);

    /// Hides the applications Transmit can only note the existence of. On by
    /// default: that is the list somebody came here to work through, and the
    /// other sixty rows are a distraction from it.
    [[nodiscard]] bool carriesDataOnly() const { return carriesDataOnly_; }
    void setCarriesDataOnly(bool only);

    /// Reads what is installed. Safe to call again; a second call while the
    /// first is running does nothing.
    Q_INVOKABLE void refresh();

    Q_INVOKABLE void setSelected(int row, bool selected);
    Q_INVOKABLE void selectAll(bool selected);

    /// Only the ones whose data can actually travel, which is the choice
    /// somebody usually means by "select all".
    Q_INVOKABLE void selectThoseThatCarryData();

    /// The current answer, ready to put on a CaptureSelection.
    [[nodiscard]] QList<core::AppSelection> selection() const;

signals:
    void loadingChanged();
    void countsChanged();
    void selectionChanged();
    void filterChanged();

private:
    struct Row {
        core::MatchedApp match;
        bool selected = true;
    };

    void adopt(const QList<core::MatchedApp>& matched);

    /// Recomputes which rows the filter lets through. Wrapped in a model reset
    /// by its callers rather than doing it itself, so a caller changing two
    /// filter settings at once resets the view once.
    void rebuildVisible();

    /// The row of `rows_` shown at `index`, or -1.
    [[nodiscard]] int sourceRow(int index) const;

    std::unique_ptr<platform::PlatformService> platform_;
    QFutureWatcher<QList<core::MatchedApp>> watcher_;
    QList<Row> rows_;

    /// Indices into `rows_`, in the order they are shown. The rows themselves
    /// are never removed by filtering: an application hidden from the list is
    /// still recorded as installed, and still remembers whether it was picked.
    QList<int> visible_;

    QString filterText_;
    bool carriesDataOnly_ = true;
    bool loading_ = false;
};

}  // namespace transmit::app
