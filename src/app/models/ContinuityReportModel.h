#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>

#include "core/continuity/ContinuityTypes.h"

namespace transmit::app {

/// The continuity report as a list the interface can show and filter.
///
/// This is the answer to the question the whole application exists to settle:
/// what came across intact, what had to be adapted, what still needs the user,
/// and what could never make the trip.
class ContinuityReportModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int fullCount READ fullCount NOTIFY countsChanged)
    Q_PROPERTY(int adaptedCount READ adaptedCount NOTIFY countsChanged)
    Q_PROPERTY(int manualCount READ manualCount NOTIFY countsChanged)
    Q_PROPERTY(int impossibleCount READ impossibleCount NOTIFY countsChanged)
    Q_PROPERTY(int gradeFilter READ gradeFilter WRITE setGradeFilter NOTIFY gradeFilterChanged)

public:
    enum Roles {
        GradeRole = Qt::UserRole + 1,
        GradeNameRole,
        DomainNameRole,
        SubjectRole,
        DetailRole,
    };

    explicit ContinuityReportModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setNotes(const QList<core::ContinuityNote>& notes);

    [[nodiscard]] int fullCount() const { return countOf(core::ContinuityGrade::Full); }
    [[nodiscard]] int adaptedCount() const { return countOf(core::ContinuityGrade::Adapted); }
    [[nodiscard]] int manualCount() const { return countOf(core::ContinuityGrade::Manual); }
    [[nodiscard]] int impossibleCount() const { return countOf(core::ContinuityGrade::Impossible); }

    /// -1 shows every grade.
    [[nodiscard]] int gradeFilter() const { return gradeFilter_; }
    void setGradeFilter(int grade);

public slots:
    /// Writes the report where the user can keep it.
    [[nodiscard]] bool saveTo(const QString& filePath) const;

signals:
    void countsChanged();
    void gradeFilterChanged();

private:
    [[nodiscard]] int countOf(core::ContinuityGrade grade) const;
    void rebuildVisible();

    QList<core::ContinuityNote> notes_;
    QList<int> visible_;
    int gradeFilter_ = -1;
};

}  // namespace transmit::app
