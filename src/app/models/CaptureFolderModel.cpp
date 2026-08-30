#include "app/models/CaptureFolderModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"

namespace transmit::app {
namespace {

/// The folders somebody keeps their own things in.
///
/// Application configuration and state are not here on purpose. They are
/// chosen by application on the step after this one, where the question is
/// "which programs", and offering the same bytes twice under two different
/// questions is how a selection ends up meaning something nobody intended.
const QList<format::PathTokenId>& userFolders() {
    static const QList<format::PathTokenId> tokens{
        format::PathTokenId::Documents, format::PathTokenId::Desktop,
        format::PathTokenId::Pictures,  format::PathTokenId::Music,
        format::PathTokenId::Videos,    format::PathTokenId::Downloads,
        format::PathTokenId::Templates,
    };
    return tokens;
}

QString displayNameFor(format::PathTokenId token) {
    switch (token) {
        case format::PathTokenId::Documents:
            return QCoreApplication::translate("Folders", "Documents");
        case format::PathTokenId::Desktop:
            return QCoreApplication::translate("Folders", "Desktop");
        case format::PathTokenId::Pictures:
            return QCoreApplication::translate("Folders", "Pictures");
        case format::PathTokenId::Music:
            return QCoreApplication::translate("Folders", "Music");
        case format::PathTokenId::Videos:
            return QCoreApplication::translate("Folders", "Videos");
        case format::PathTokenId::Downloads:
            return QCoreApplication::translate("Folders", "Downloads");
        case format::PathTokenId::Templates:
            return QCoreApplication::translate("Folders", "Templates");
        default:
            break;
    }
    return QString::fromUtf8(format::tokenName(token).data(),
                             static_cast<qsizetype>(format::tokenName(token).size()));
}

core::CaptureRoot rootFor(format::PathTokenId token) {
    core::CaptureRoot root;
    root.token = token;
    root.domain = core::DomainId::UserData;
    root.recursive = true;
    return root;
}

}  // namespace

CaptureFolderModel::CaptureFolderModel(QObject* parent) : QAbstractListModel(parent) {
    platform_ = platform::PlatformService::create();
    const format::PathTokenMap folders = platform_->knownFolders();

    for (const format::PathTokenId token : userFolders()) {
        Folder folder;
        folder.token = token;
        folder.displayName = displayNameFor(token);
        if (const auto base = folders.base(token)) {
            folder.path = core::fromUtf8(*base);
            folder.present = QFileInfo(folder.path).isDir();
        }
        // A folder this system does not have is listed and turned off rather
        // than hidden, so the list is the same everywhere and somebody
        // comparing two machines can see what the other one had.
        folder.selected = folder.present;
        folders_.push_back(folder);
    }

    connect(&watcher_, &QFutureWatcher<QHash<int, QPair<quint64, quint64>>>::finished, this,
            &CaptureFolderModel::handleMeasured);
}

CaptureFolderModel::~CaptureFolderModel() {
    cancelToken_.cancel();
    watcher_.waitForFinished();
}

int CaptureFolderModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(folders_.size());
}

QVariant CaptureFolderModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= folders_.size()) {
        return {};
    }
    const Folder& folder = folders_.at(index.row());
    switch (role) {
        case TokenNameRole:
            return QString::fromUtf8(
                format::tokenName(folder.token).data(),
                static_cast<qsizetype>(format::tokenName(folder.token).size()));
        case DisplayNameRole:
            return folder.displayName;
        case PathRole:
            return folder.path;
        case SizeBytesRole:
            return static_cast<double>(folder.sizeBytes);
        case SizeTextRole:
            if (!folder.present) {
                return QCoreApplication::translate("Folders", "not on this computer");
            }
            if (!measured_) {
                return QString();
            }
            return QCoreApplication::translate("Folders", "%1 in %n file(s)", nullptr,
                                               static_cast<int>(folder.fileCount))
                .arg(core::formatBytes(folder.sizeBytes));
        case FileCountRole:
            return static_cast<double>(folder.fileCount);
        case SelectedRole:
            return folder.selected;
        case PresentRole:
            return folder.present;
        default:
            break;
    }
    return {};
}

QHash<int, QByteArray> CaptureFolderModel::roleNames() const {
    return {
        {TokenNameRole, "tokenName"}, {DisplayNameRole, "displayName"},
        {PathRole, "path"},           {SizeBytesRole, "sizeBytes"},
        {SizeTextRole, "sizeText"},   {FileCountRole, "fileCount"},
        {SelectedRole, "selected"},   {PresentRole, "present"},
    };
}

int CaptureFolderModel::selectedCount() const {
    return static_cast<int>(std::count_if(folders_.begin(), folders_.end(),
                                          [](const Folder& f) { return f.selected; }));
}

bool CaptureFolderModel::isNarrowed() const {
    return std::any_of(folders_.begin(), folders_.end(),
                       [](const Folder& f) { return f.present && !f.selected; });
}

QString CaptureFolderModel::selectionSummary() const {
    QStringList names;
    quint64 bytes = 0;
    for (const Folder& folder : folders_) {
        if (!folder.selected || !folder.present) {
            continue;
        }
        names << folder.displayName;
        bytes += folder.sizeBytes;
    }

    if (names.isEmpty()) {
        return QCoreApplication::translate("Folders", "No folders of your own");
    }

    // Two names and a count, rather than seven names nobody reads.
    QString listed = names.size() <= 2
                         ? names.join(QCoreApplication::translate("Folders", " and "))
                         : QCoreApplication::translate("Folders", "%1 and %n other(s)", nullptr,
                                                       static_cast<int>(names.size() - 2))
                               .arg(names.mid(0, 2).join(QStringLiteral(", ")));
    if (!measured_) {
        return listed;
    }
    return QCoreApplication::translate("Folders", "%1 - %2").arg(listed, core::formatBytes(bytes));
}

void CaptureFolderModel::measure() {
    if (measuring_) {
        return;
    }
    measuring_ = true;
    cancelToken_.reset();
    emit measuringChanged();

    core::CaptureSelection selection;
    selection.domains = {static_cast<int>(core::DomainId::UserData)};
    selection.scope.excludePatterns = core::ScopeRule::defaultExcludes();
    for (const Folder& folder : folders_) {
        if (folder.present) {
            selection.roots.push_back(rootFor(folder.token));
        }
    }

    const platform::PlatformService* platform = platform_.get();
    core::CancelToken* token = &cancelToken_;
    watcher_.setFuture(
        QtConcurrent::run([platform, token, selection]() -> QHash<int, QPair<quint64, quint64>> {
            // One walk for every folder, adding up per folder as it goes. The
            // scan already knows which known folder each item belongs to, so a
            // scan each would be the same work done seven times.
            const core::ScanService scanner(*platform);
            core::CancelToken& cancel = *token;
            const core::ScanResult result = scanner.scan(selection, cancel);

            QHash<int, QPair<quint64, quint64>> byToken;
            for (const core::ScannedItem& item : result.items) {
                if (item.type != format::EntryType::File) {
                    continue;
                }
                auto& entry = byToken[static_cast<int>(item.tokenPath.token)];
                entry.first += item.size;
                entry.second += 1;
            }
            return byToken;
        }));
}

void CaptureFolderModel::handleMeasured() {
    const QHash<int, QPair<quint64, quint64>> byToken = watcher_.result();

    for (qsizetype i = 0; i < folders_.size(); ++i) {
        Folder& folder = folders_[i];
        const auto found = byToken.constFind(static_cast<int>(folder.token));
        folder.sizeBytes = found == byToken.constEnd() ? 0 : found->first;
        folder.fileCount = found == byToken.constEnd() ? 0 : found->second;
    }

    measuring_ = false;
    measured_ = true;
    if (!folders_.isEmpty()) {
        emit dataChanged(index(0), index(static_cast<int>(folders_.size()) - 1),
                         {SizeBytesRole, SizeTextRole, FileCountRole});
    }
    emit measuringChanged();
    emit measuredChanged();
    emit selectionChanged();
}

void CaptureFolderModel::setSelected(int row, bool selected) {
    if (row < 0 || row >= folders_.size()) {
        return;
    }
    Folder& folder = folders_[row];
    // A folder that is not there cannot be taken, and letting it be ticked
    // would promise something the capture then quietly does not do.
    if (!folder.present || folder.selected == selected) {
        return;
    }
    folder.selected = selected;
    emit dataChanged(index(row), index(row), {SelectedRole});
    emit selectionChanged();
}

void CaptureFolderModel::selectAll() {
    bool changed = false;
    for (qsizetype i = 0; i < folders_.size(); ++i) {
        if (folders_[i].present && !folders_[i].selected) {
            folders_[i].selected = true;
            changed = true;
        }
    }
    if (changed) {
        emit dataChanged(index(0), index(static_cast<int>(folders_.size()) - 1), {SelectedRole});
        emit selectionChanged();
    }
}

void CaptureFolderModel::selectNone() {
    bool changed = false;
    for (qsizetype i = 0; i < folders_.size(); ++i) {
        if (folders_[i].selected) {
            folders_[i].selected = false;
            changed = true;
        }
    }
    if (changed) {
        emit dataChanged(index(0), index(static_cast<int>(folders_.size()) - 1), {SelectedRole});
        emit selectionChanged();
    }
}

QList<core::CaptureRoot> CaptureFolderModel::chosenRoots() const {
    QList<core::CaptureRoot> roots;
    for (const Folder& folder : folders_) {
        if (folder.selected && folder.present) {
            roots.push_back(rootFor(folder.token));
        }
    }
    return roots;
}

}  // namespace transmit::app
