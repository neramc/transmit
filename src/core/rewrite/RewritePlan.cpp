#include "core/rewrite/RewritePlan.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include "core/utils/Logging.h"

namespace transmit::core {

void RewritePlan::add(RewriteEdit edit) {
    // A removal has no new value to differ from the old one, so the guard that
    // throws away a change that changes nothing does not apply to it.
    if (edit.kind == EditKind::Remove || edit.oldValue != edit.newValue) {
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

QStringList RewritePlan::files() const {
    QStringList paths;
    QSet<QString> seen;
    for (const RewriteEdit& edit : edits_) {
        if (!seen.contains(edit.filePath)) {
            seen.insert(edit.filePath);
            paths.push_back(edit.filePath);
        }
    }
    return paths;
}

int RewritePlan::discardBackups(const QStringList& files) {
    int removed = 0;
    for (const QString& path : files) {
        if (QFile::remove(path + QLatin1String(kBackupSuffix))) {
            ++removed;
        }
    }
    return removed;
}

int RewritePlan::apply(QStringList* errors) const {
    // The rewriters have already written the new contents to a staging file
    // next to the target; applying is the swap, which is what makes the whole
    // pass reversible.
    QSet<QString> files;
    QSet<QString> removals;
    for (const RewriteEdit& edit : edits_) {
        files.insert(edit.filePath);
        if (edit.kind == EditKind::Remove) {
            removals.insert(edit.filePath);
        }
    }

    int changed = 0;
    for (const QString& path : files) {
        // A file that is to go has no staged copy: what replaces it is
        // nothing. It still goes through the backup, so the whole pass stays
        // reversible - a file this deleted is one the undo has to put back.
        if (removals.contains(path)) {
            if (!QFile::exists(path)) {
                continue;
            }
            const QString backup = path + QLatin1String(kBackupSuffix);
            QFile::remove(backup);
            if (!QFile::copy(path, backup)) {
                if (errors != nullptr) {
                    *errors << QCoreApplication::translate("Rewrite",
                                                           "Could not set aside the original of %1")
                                   .arg(path);
                }
                continue;
            }
            if (!QFile::remove(path)) {
                if (errors != nullptr) {
                    *errors
                        << QCoreApplication::translate("Rewrite", "Could not remove %1").arg(path);
                }
                QFile::remove(backup);
                continue;
            }
            ++changed;
            continue;
        }

        const QString staged = path + QStringLiteral(".transmit-staged");
        if (!QFile::exists(staged)) {
            continue;  // nothing was actually produced for this file
        }

        const QString backup = path + QLatin1String(kBackupSuffix);
        QFile::remove(backup);

        // The original is copied aside, not moved. Moving it means that
        // between the two renames there is no file at `path` at all, and the
        // only copy of the user's settings is under a name they have never
        // heard of - so if the second rename fails, and the recovery rename
        // fails too, the file has silently vanished from where they expect it.
        // A copy costs one pass over a settings file and removes the hole.
        const bool originalExists = QFile::exists(path);
        if (originalExists && !QFile::copy(path, backup)) {
            if (errors != nullptr) {
                *errors << QCoreApplication::translate("Rewrite",
                                                       "Could not set aside the original of %1")
                               .arg(path);
            }
            QFile::remove(staged);
            continue;
        }
        if (originalExists && !QFile::remove(path)) {
            if (errors != nullptr) {
                *errors << QCoreApplication::translate("Rewrite", "Could not update %1").arg(path);
            }
            QFile::remove(backup);
            QFile::remove(staged);
            continue;
        }

        if (!QFile::rename(staged, path)) {
            // Put the original back. The backup is still there either way, so
            // this cannot be the step that loses it.
            const bool recovered = !originalExists || QFile::copy(backup, path);
            if (errors != nullptr) {
                *errors << (recovered
                                ? QCoreApplication::translate("Rewrite", "Could not update %1")
                                      .arg(path)
                                : QCoreApplication::translate(
                                      "Rewrite",
                                      "Could not update %1, and putting the original back "
                                      "failed too - it is at %2")
                                      .arg(path, backup));
            }
            QFile::remove(staged);
            continue;
        }
        ++changed;
    }

    qCInfo(logRewrite) << "rewrote or removed" << changed << "files";
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
            continue;
        }

        // A rename can fail where a copy still works - across a filesystem
        // boundary, or with something holding the name open - so that is tried
        // before giving up.
        if (QFile::copy(backup, path)) {
            QFile::remove(backup);
            ++restored;
            continue;
        }

        // And if neither worked, say where the file is. The remove above has
        // already happened, so the only copy of what the user had is under a
        // name they have never heard of; a message that does not name it is
        // the same as the file having vanished.
        if (errors != nullptr) {
            *errors << QCoreApplication::translate(
                           "Rewrite", "Could not put back %1 - the original is still at %2")
                           .arg(path, backup);
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
        note.subject =
            QStringLiteral("%1 - %2").arg(QFileInfo(edit.filePath).fileName(), edit.location);
        note.detail =
            edit.kind == EditKind::Remove
                ? QCoreApplication::translate(
                      "Rewrite", "Removed, because %1 rebuilds it for this machine on first start.")
                      .arg(edit.appId)
                : QCoreApplication::translate("Rewrite", "Pointed at \"%1\" instead of \"%2\".")
                      .arg(edit.newValue, edit.oldValue);
        notes.push_back(note);
    }
    return notes;
}

}  // namespace transmit::core
