#include "app/models/AppCatalogModel.h"

#include <QtConcurrent/QtConcurrent>

#include <algorithm>

#include "core/recipe/RecipeCatalog.h"
#include "core/utils/Conversions.h"

namespace transmit::app {
namespace {

/// Everything the interface needs about one application, worked out on a
/// worker thread so the window keeps repainting while it happens.
QList<core::MatchedApp> detect(platform::PlatformService* platform) {
    core::RecipeCatalog catalog;
    catalog.loadDefaults();

    const format::PathTokenMap folders = platform->knownFolders();
    const format::OsFamily os = platform->environment().os;

    QList<core::MatchedApp> matched = catalog.match(platform->installedApplications(), os);
    matched += catalog.matchByStateOnly(matched, os, folders);
    catalog.noteWhichHaveState(matched, os, folders);

    std::sort(matched.begin(), matched.end(),
              [](const core::MatchedApp& a, const core::MatchedApp& b) {
                  // Those that can bring their data first: that is the choice
                  // somebody is here to make, and the rest are a list to skim.
                  const bool aCarries = a.recipe.portability.carriesData && a.hasState;
                  const bool bCarries = b.recipe.portability.carriesData && b.hasState;
                  if (aCarries != bCarries) {
                      return aCarries;
                  }
                  return a.recipe.displayName.localeAwareCompare(b.recipe.displayName) < 0;
              });
    return matched;
}

bool carriesData(const core::MatchedApp& match) {
    return match.recipe.portability.carriesData && match.hasState;
}

}  // namespace

AppCatalogModel::AppCatalogModel(QObject* parent) : QAbstractListModel(parent) {
    platform_ = platform::PlatformService::create();

    connect(&watcher_, &QFutureWatcher<QList<core::MatchedApp>>::finished, this,
            [this]() { adopt(watcher_.result()); });
}

AppCatalogModel::~AppCatalogModel() {
    // The worker holds a raw pointer to the platform service, which is about
    // to go away with this object.
    watcher_.waitForFinished();
}

int AppCatalogModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(visible_.size());
}

int AppCatalogModel::sourceRow(int index) const {
    return index >= 0 && index < visible_.size() ? visible_.at(index) : -1;
}

QHash<int, QByteArray> AppCatalogModel::roleNames() const {
    return {
        {AppIdRole, "appId"},
        {DisplayNameRole, "displayName"},
        {CarriesDataRole, "carriesData"},
        {HasStateRole, "hasState"},
        {InstalledRole, "installed"},
        {StateSummaryRole, "stateSummary"},
        {GradeRole, "grade"},
        {GradeNameRole, "gradeName"},
        {NoteRole, "note"},
        {SelectedRole, "selected"},
    };
}

QVariant AppCatalogModel::data(const QModelIndex& index, int role) const {
    const int source = index.isValid() ? sourceRow(index.row()) : -1;
    if (source < 0) {
        return {};
    }
    const Row& row = rows_.at(source);
    const core::AppRecipe& recipe = row.match.recipe;

    switch (role) {
        case AppIdRole:
            return recipe.id;
        case DisplayNameRole:
            return recipe.displayName;
        case CarriesDataRole:
            return carriesData(row.match);
        case HasStateRole:
            return row.match.hasState;
        case InstalledRole:
            return !row.match.installation.id.isEmpty();
        case StateSummaryRole: {
            QStringList roots;
            for (const core::RecipeStatePath& state : recipe.state) {
                roots << state.id;
            }
            return roots.join(QStringLiteral(", "));
        }
        case GradeRole:
            return static_cast<int>(recipe.expectedGrade);
        case GradeNameRole:
            return core::continuityGradeName(recipe.expectedGrade);
        case NoteRole:
            return recipe.note;
        case SelectedRole:
            return row.selected;
        default:
            return {};
    }
}

int AppCatalogModel::carriesDataCount() const {
    return static_cast<int>(std::count_if(
        rows_.constBegin(), rows_.constEnd(),
        [](const Row& row) { return carriesData(row.match); }));
}

int AppCatalogModel::selectedCount() const {
    return static_cast<int>(std::count_if(rows_.constBegin(), rows_.constEnd(),
                                          [](const Row& row) { return row.selected; }));
}

void AppCatalogModel::setFilterText(const QString& text) {
    if (filterText_ == text) {
        return;
    }
    filterText_ = text;
    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit filterChanged();
    emit countsChanged();
}

void AppCatalogModel::setCarriesDataOnly(bool only) {
    if (carriesDataOnly_ == only) {
        return;
    }
    carriesDataOnly_ = only;
    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit filterChanged();
    emit countsChanged();
}

void AppCatalogModel::rebuildVisible() {
    visible_.clear();
    visible_.reserve(rows_.size());

    const QString needle = filterText_.trimmed();
    for (int i = 0; i < rows_.size(); ++i) {
        const core::MatchedApp& match = rows_.at(i).match;
        if (carriesDataOnly_ && !carriesData(match)) {
            continue;
        }
        // Matched against the id as well as the name, so somebody who knows
        // the application as "chromium" finds it under whatever the catalog
        // decided to call it.
        if (!needle.isEmpty()
            && !match.recipe.displayName.contains(needle, Qt::CaseInsensitive)
            && !match.recipe.id.contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        visible_.push_back(i);
    }
}

void AppCatalogModel::refresh() {
    if (loading_) {
        return;
    }
    loading_ = true;
    emit loadingChanged();

    platform::PlatformService* const platform = platform_.get();
    watcher_.setFuture(QtConcurrent::run([platform] { return detect(platform); }));
}

void AppCatalogModel::adopt(const QList<core::MatchedApp>& matched) {
    // What was chosen before survives a refresh: a stick plugged in half way
    // through should not undo somebody's answers.
    QHash<QString, bool> chosen;
    for (const Row& row : rows_) {
        chosen.insert(row.match.recipe.id, row.selected);
    }

    beginResetModel();
    rows_.clear();
    rows_.reserve(matched.size());
    for (const core::MatchedApp& match : matched) {
        Row row;
        row.match = match;
        // Anything whose data can travel starts chosen. The rest are still
        // listed - and still recorded as installed - but there is nothing of
        // theirs to carry, so a tick beside them would promise something.
        row.selected = chosen.value(match.recipe.id, carriesData(match));
        rows_.push_back(row);
    }
    rebuildVisible();
    endResetModel();

    loading_ = false;
    emit loadingChanged();
    emit countsChanged();
    emit selectionChanged();
}

void AppCatalogModel::setSelected(int row, bool selected) {
    const int source = sourceRow(row);
    if (source < 0 || rows_[source].selected == selected) {
        return;
    }
    rows_[source].selected = selected;
    const QModelIndex changed = index(row);
    emit dataChanged(changed, changed, {SelectedRole});
    emit selectionChanged();
}

void AppCatalogModel::selectAll(bool selected) {
    // What is on screen, not what is behind the filter. Somebody who has
    // typed "fire" into the search and pressed None means those rows; silently
    // clearing the sixty they cannot see would be the sort of surprise that is
    // only discovered on the other machine.
    if (visible_.isEmpty()) {
        return;
    }
    for (const int source : visible_) {
        rows_[source].selected = selected;
    }
    emit dataChanged(index(0), index(static_cast<int>(visible_.size()) - 1), {SelectedRole});
    emit selectionChanged();
}

void AppCatalogModel::selectThoseThatCarryData() {
    if (visible_.isEmpty()) {
        return;
    }
    for (const int source : visible_) {
        rows_[source].selected = carriesData(rows_.at(source).match);
    }
    emit dataChanged(index(0), index(static_cast<int>(visible_.size()) - 1), {SelectedRole});
    emit selectionChanged();
}

QList<core::AppSelection> AppCatalogModel::selection() const {
    // Every row, not the visible ones: an application the filter is hiding
    // still has an answer, and leaving it out of the list would read as
    // "never heard of it" rather than "not chosen".
    QList<core::AppSelection> answers;
    answers.reserve(rows_.size());

    for (const Row& row : rows_) {
        core::AppSelection answer;
        answer.appId = row.match.recipe.id;
        answer.captureState = row.selected && carriesData(row.match);

        // Always. The list of what was installed costs a few hundred bytes for
        // a whole machine and is the only thing that lets a restore offer
        // anything at all, so declining to carry an application's data is not
        // the same as declining to mention it.
        answer.recordForReinstall = true;
        answers.push_back(answer);
    }
    return answers;
}

}  // namespace transmit::app
