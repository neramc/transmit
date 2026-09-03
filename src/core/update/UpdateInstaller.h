#pragma once

#include <QByteArray>
#include <QString>

#include "core/update/InstallKind.h"

namespace transmit::core {

/// What happened when a staged update was applied.
struct InstallOutcome {
    /// The installed copy is now the new version, or will be as soon as the
    /// program restarts.
    bool applied = false;

    /// Restarting is what makes the new version the running one.
    bool needsRestart = false;

    /// The staged file, when it was left for somebody to open themselves.
    /// Set for the shapes of install this cannot replace on its own.
    QString handedOver;

    /// The file the previous version was moved to, so a failed start can be
    /// undone. Empty when there was nothing to keep.
    QString previous;

    QString problem;
};

/// Puts a verified, staged download in place.
///
/// Nothing here downloads or checks anything: by the time it is called the file
/// has already been matched against the digest in a signed feed. It is a
/// separate step so that the thing which replaces a program on disk does one
/// job, and so it can be pointed at a temporary directory in a test rather
/// than at the running program.
class UpdateInstaller {
public:
    /// Replaces `target` with `staged`. `target` is what replaceableTarget()
    /// returned: the AppImage, the installed directory, the .app.
    ///
    /// `expected` is the BLAKE2b-256 from the signed feed. The download was
    /// already checked against it; it is checked again here because the file
    /// has been sitting on disk in a directory anything running as this user
    /// could write to, and the moment before it becomes the program is the
    /// last moment the check is worth anything. Pass an empty value only where
    /// there is genuinely nothing to compare against.
    [[nodiscard]] static InstallOutcome apply(const QString& staged, const QString& target,
                                              InstallKind kind, const QByteArray& expected = {});

    /// Puts back what apply() moved aside. Safe to call when there is nothing
    /// to put back.
    [[nodiscard]] static bool undo(const InstallOutcome& outcome, const QString& target);
};

}  // namespace transmit::core
