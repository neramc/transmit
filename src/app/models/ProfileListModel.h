#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>

#include "core/services/ProfileService.h"

namespace transmit::app {

/// The capture profiles offered on the first page of the wizard.
class ProfileListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        DisplayNameRole,
        DescriptionRole,
        SizeHintRole,
        DomainSummaryRole,
    };

    explicit ProfileListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE [[nodiscard]] QString idAt(int row) const;

private:
    QList<core::CaptureProfile> profiles_;
};

}  // namespace transmit::app
