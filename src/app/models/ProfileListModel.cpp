#include "app/models/ProfileListModel.h"

#include "core/utils/Conversions.h"

using transmit::core::formatBytes;
using transmit::core::fromUtf8;

namespace transmit::app {

ProfileListModel::ProfileListModel(QObject* parent)
    : QAbstractListModel(parent), profiles_(core::ProfileService::builtInProfiles()) {}

int ProfileListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(profiles_.size());
}

QVariant ProfileListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= profiles_.size()) {
        return {};
    }
    const core::CaptureProfile& profile = profiles_.at(index.row());

    switch (role) {
        case IdRole:
            return profile.id;
        case DisplayNameRole:
            return profile.displayName;
        case DescriptionRole:
            return profile.description;
        case SizeHintRole:
            return profile.sizeHint;
        case DomainSummaryRole: {
            // Names the user recognises, rather than the internal domain ids.
            QStringList parts;
            if (profile.selection.includes(core::DomainId::UserData)) {
                parts << tr("your files");
            }
            if (profile.selection.includes(core::DomainId::AppState)) {
                parts << tr("application data");
            }
            if (profile.selection.includes(core::DomainId::SystemSettings)) {
                parts << tr("system settings");
            }
            if (profile.selection.includes(core::DomainId::AppInventory)) {
                parts << tr("installed programs");
            }
            if (profile.selection.includes(core::DomainId::Secrets)) {
                parts << tr("saved passwords");
            }
            return parts.join(QStringLiteral(", "));
        }
        default:
            return {};
    }
}

QHash<int, QByteArray> ProfileListModel::roleNames() const {
    return {{IdRole, "profileId"},
            {DisplayNameRole, "displayName"},
            {DescriptionRole, "description"},
            {SizeHintRole, "sizeHint"},
            {DomainSummaryRole, "domainSummary"}};
}

QString ProfileListModel::idAt(int row) const {
    if (row < 0 || row >= profiles_.size()) {
        return {};
    }
    return profiles_.at(row).id;
}

}  // namespace transmit::app
