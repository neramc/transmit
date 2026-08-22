#include "core/rewrite/RewritePlan.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include "core/utils/Logging.h"

namespace transmit::core {

void RewritePlan::add(RewriteEdit edit) {
    if (edit.oldValue != edit.newValue) {
        edits_.push_back(std::move(edit));
    }
}

int RewritePlan::fileCount() const {
    QSet<QString> files;
    for (const RewriteEdit& edit : edits_) {
        files.insert(edit.filePath);
    }
    return static_cast<int>(files.size());
}

int RewritePlan::apply(QStringList* errors) const {
    // The rewriters have already written the new contents to a staging file
    // next to the target; applying is the swap, which is what makes the whole
    // pass reversible.
    QSet<QString> files;
    for (const RewriteEdit& edit : edits_) {
        files.insert(edit.filePath);
    }

    int changed = 0;
    for (const QString& path : files) {
        const QString staged = path + QStringLiteral(".transmit-staged");
        if (!QFile::exists(staged)) {
            continue;  // nothing was actually produced for this file
        }

        const QString backup = path + QLatin1String(kBackupSuffix);
        QFile::remove(backup);
        if (QFile::exists(path) && !QFile::rename(path, backup)) {
            if (errors != nullptr) {
                *errors << QCoreApplication::translate(
                               "Rewrite", "Could not set aside the original of %1").arg(path);
            }
            QFile::remove(staged);
            continue;
        }
        if (!QFile::rename(staged, path)) {
            QFile::rename(backup, path);  // put it back rather than leaving a hole
            if (errors != nullptr) {
                *errors << QCoreApplication::translate("Rewrite", "Could not update %1").arg(path);
            }
            continue;
        }
        ++changed;
    }

    qCInfo(logRewrite) << "rewrote paths inside" << changed << "files";
    return changed;
}

int RewritePlan::revert(QStringList* errors) const {
    QSet<QString> files;
    for (const RewriteEdit& edit : edits_) {
        files.insert(edit.filePath);
    }

    int restored = 0;
    for (const QString& path : files) {
        const QString backup = path + QLatin1String(kBackupSuffix);
        if (!QFile::exists(backup)) {
            continue;
        }
        QFile::remove(path);
        if (QFile::rename(backup, path)) {
            ++restored;
        } else if (errors != nullptr) {
            *errors << QCoreApplication::translate("Rewrite", "Could not put back %1").arg(path);
        }
    }
    return restored;
}

QList<ContinuityNote> RewritePlan::toNotes() const {
    QList<ContinuityNote> notes;
    notes.reserve(edits_.size());

    for (const RewriteEdit& edit : edits_) {
        ContinuityNote note;
        note.grade = ContinuityGrade::Adapted;
        note.domain = DomainId::AppState;
        note.subject = QStringLiteral("%1 - %2")
                           .arg(QFileInfo(edit.filePath).fileName(), edit.location);
        note.detail = QCoreApplication::translate("Rewrite", "Pointed at \"%1\" instead of \"%2\".")
                          .arg(edit.newValue, edit.oldValue);
        notes.push_back(note);
    }
    return notes;
}

}  // namespace transmit::core
