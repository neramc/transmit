#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "format/Result.h"

namespace transmit::core {

/// Records what a restore is about to change, so it can be undone.
///
/// A restore writes into the places a person actually keeps things. However
/// carefully it is previewed, the honest position is that they might still want
/// it reversed - so before anything is written, whatever is about to be
/// overwritten is captured into an archive of its own, and whatever is about to
/// be created is noted by name.
///
/// Undoing then means putting the first set back and deleting the second.
class RollbackWriter {
public:
    struct CaptureResult {
        /// Empty when nothing would be overwritten and there is nothing
        /// to undo.
        QString archivePath;

        /// Paths that exist and could not be read in full, so they are
        /// not in the archive. The restore must leave these alone: it
        /// would be replacing something it cannot put back.
        QStringList unbackedUp;
    };

    /// `targets` is every path the restore intends to write.
    [[nodiscard]] static format::Result<CaptureResult> capture(const QStringList& targets,
                                                               const QString& directory);

    /// Reverses a restore from the archive `capture` produced.
    struct UndoResult {
        int filesRestored = 0;
        int filesRemoved = 0;
        QStringList errors;
    };

    [[nodiscard]] static format::Result<UndoResult> undo(const QString& archivePath);

    /// Where rollback archives are kept, relative to the restore destination.
    static constexpr const char* kDirectoryName = ".transmit";

    /// Marks the paths that did not exist before, and so should be deleted
    /// rather than put back.
    static constexpr const char* kCreatedPayloadKind = "rollback.created.v1";
};

}  // namespace transmit::core
