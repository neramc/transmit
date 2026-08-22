#include "app/models/ContinuityReportModel.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "core/utils/Conversions.h"

using transmit::core::formatBytes;
using transmit::core::fromUtf8;

namespace transmit::app {

ContinuityReportModel::ContinuityReportModel(QObject* parent) : QAbstractListModel(parent) {}

int ContinuityReportModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(visible_.size());
}

QVariant ContinuityReportModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= visible_.size()) {
        return {};
    }
    const core::ContinuityNote& note = notes_.at(visible_.at(index.row()));

    switch (role) {
        case GradeRole:
            return static_cast<int>(note.grade);
        case GradeNameRole:
            return core::continuityGradeName(note.grade);
        case DomainNameRole:
            return fromUtf8(format::domainName(note.domain));
        case SubjectRole:
            return note.subject;
        case DetailRole:
            return note.detail;
        default:
            return {};
    }
}

QHash<int, QByteArray> ContinuityReportModel::roleNames() const {
    return {{GradeRole, "grade"},
            {GradeNameRole, "gradeName"},
            {DomainNameRole, "domainName"},
            {SubjectRole, "subject"},
            {DetailRole, "detail"}};
}

void ContinuityReportModel::setNotes(const QList<core::ContinuityNote>& notes) {
    beginResetModel();
    notes_ = notes;
    rebuildVisible();
    endResetModel();
    emit countsChanged();
}

void ContinuityReportModel::setGradeFilter(int grade) {
    if (gradeFilter_ == grade) {
        return;
    }
    beginResetModel();
    gradeFilter_ = grade;
    rebuildVisible();
    endResetModel();
    emit gradeFilterChanged();
}

void ContinuityReportModel::rebuildVisible() {
    visible_.clear();
    for (int i = 0; i < notes_.size(); ++i) {
        if (gradeFilter_ < 0 || static_cast<int>(notes_.at(i).grade) == gradeFilter_) {
            visible_.push_back(i);
        }
    }
}

int ContinuityReportModel::countOf(core::ContinuityGrade grade) const {
    int count = 0;
    for (const core::ContinuityNote& note : notes_) {
        if (note.grade == grade) {
            ++count;
        }
    }
    return count;
}

bool ContinuityReportModel::saveTo(const QString& filePath) const {
    QJsonArray entries;
    for (const core::ContinuityNote& note : notes_) {
        QJsonObject entry;
        entry[QStringLiteral("grade")] = core::continuityGradeName(note.grade);
        entry[QStringLiteral("domain")] = fromUtf8(format::domainName(note.domain));
        entry[QStringLiteral("subject")] = note.subject;
        entry[QStringLiteral("detail")] = note.detail;
        entries.append(entry);
    }

    QJsonObject root;
    root[QStringLiteral("generatedBy")] = QStringLiteral("Transmit " TRANSMIT_VERSION);
    root[QStringLiteral("notes")] = entries;

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

}  // namespace transmit::app
