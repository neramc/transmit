#pragma once

#include <QList>
#include <QString>

#include "core/continuity/ContinuityTypes.h"

namespace transmit::core {

/// One value inside one file that a restore would change.
struct RewriteEdit {
    QString filePath;  ///< the restored file on this machine
    QString location;  ///< which key, section or line - human readable
    QString oldValue;
    QString newValue;
    QString appId;  ///< the recipe that asked for this
};

/// Everything a restore intends to change inside restored files, gathered
/// before any of it happens.
///
/// Rewriting paths inside a user's configuration is the most invasive thing
/// Transmit does, so it is staged: the plan is built and shown, and only then
/// applied. Every applied file keeps a backup, so the whole pass can be undone
/// without re-running the restore.
class RewritePlan {
public:
    /// Suffix of the backup written next to each modified file.
    static constexpr const char* kBackupSuffix = ".transmit-backup";

    void add(RewriteEdit edit);

    [[nodiscard]] const QList<RewriteEdit>& edits() const noexcept { return edits_; }
    [[nodiscard]] bool isEmpty() const noexcept { return edits_.isEmpty(); }
    [[nodiscard]] int fileCount() const;

    /// Writes the changes, keeping a backup of each original. Returns the
    /// number of files changed; `errors` collects anything that failed so one
    /// unwritable file does not abort the rest.
    int apply(QStringList* errors = nullptr) const;

    /// Restores every backup this plan created.
    int revert(QStringList* errors = nullptr) const;

    /// Every file the plan touches, once each.
    [[nodiscard]] QStringList files() const;

    /// Throws away the kept originals. For when the user has decided the
    /// restore is staying: until then the backups are what makes it
    /// reversible, and after that they are litter in someone's config folder.
    static int discardBackups(const QStringList& files);

    /// Turns the plan into report entries, so the user sees these changes in
    /// the same place as everything else.
    [[nodiscard]] QList<ContinuityNote> toNotes() const;

private:
    QList<RewriteEdit> edits_;
};

}  // namespace transmit::core
