#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

#include "core/continuity/ContinuityTypes.h"
#include "core/services/ScanService.h"
#include "core/services/VerifyService.h"

namespace transmit::core {

/// Why one file could not be recovered.
enum class RepairObstacle {
    SourceMissing,  ///< the file is no longer on this machine
    SourceChanged,  ///< it is here, and it is not the file that was captured
    SourceUnreadable,
};

QString repairObstacleName(RepairObstacle obstacle);

struct RepairFailure {
    QString path;        ///< as the archive names it
    QString sourcePath;  ///< where it was looked for on this machine
    RepairObstacle obstacle = RepairObstacle::SourceMissing;
    QString detail;
};

struct RepairRequest {
    /// The archive, or any one of its parts.
    QString archivePath;
    QString passphrase;

    /// Which files to recover, by the path the archive names them by. Empty
    /// runs a verification first and repairs whatever it finds.
    QStringList paths;
};

struct RepairReport {
    bool succeeded = false;
    QString errorMessage;

    /// The repair archive written, empty when nothing needed repairing.
    QString repairPath;

    quint64 filesNeedingRepair = 0;
    quint64 filesRepaired = 0;
    QList<RepairFailure> failures;

    [[nodiscard]] bool everythingRecovered() const {
        return succeeded && filesRepaired == filesNeedingRepair;
    }
};

/// Recovers the files a drive damaged, from the machine they came from.
///
/// The damaged archive is never touched. Its footer and part lengths are
/// computed over the whole set, so writing a corrected file back into it would
/// invalidate the thing being fixed; instead the recovered files go into
/// `name.txa.repair`, a small archive of their own that every reader picks up
/// automatically. That also makes the operation safe to interrupt: at no point
/// does the original stop being whatever it already was.
///
/// A file is only recovered when what is on this machine still hashes to what
/// the archive recorded. A file that has been edited since the capture cannot
/// repair that capture - putting the new version in would produce an archive
/// that passes every check and does not hold what it says it holds.
class RepairService {
public:
    using ProgressCallback = std::function<void(const ProgressUpdate&)>;

    /// Takes no platform service on purpose. Where a file belongs is read
    /// from the archive's own recorded token bases, not from this machine -
    /// that is what lets a capture be repaired from a different account, or
    /// from a home directory that has since been renamed.
    [[nodiscard]] RepairReport run(const RepairRequest& request, CancelToken& cancelToken,
                                   const ProgressCallback& progress = {}) const;
};

}  // namespace transmit::core
