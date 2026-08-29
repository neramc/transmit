#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

#include "core/continuity/ContinuityTypes.h"
#include "core/services/ScanService.h"

namespace transmit::core {

/// How one file came back.
enum class VerifyStatus {
    Ok,
    ContentMismatch,  ///< the bytes are not the bytes that went in
    Md5Mismatch,      ///< BLAKE2b agreed and MD5 did not, which should not happen
    Unreadable,       ///< the drive would not give it back at all
};

QString verifyStatusName(VerifyStatus status);

/// One file's result. Only failures are kept in the report - a hundred
/// thousand rows saying "fine" is not a report, it is a log.
struct VerifyFileResult {
    QString path;
    QString appId;
    VerifyStatus status = VerifyStatus::Ok;
    quint64 size = 0;

    /// How many reads it took. A stick that answers on the third attempt is
    /// working and dying, and that is worth seeing before the machine it came
    /// from is wiped.
    int attempts = 1;
    QString detail;

    /// The bytes came from the repair archive beside this one rather than
    /// from the archive itself. The file is readable; the archive is still
    /// damaged, and both halves of that matter.
    bool fromRepair = false;
};

/// What a part of the archive looked like on the way back.
struct VerifyPartResult {
    QString path;
    quint64 size = 0;
    bool endsMatched = true;  ///< the first and last 4 KiB read back the same twice
    bool md5Matched = true;   ///< against the .md5 sidecar, when there is one
    QString detail;
};

struct VerifyRequest {
    /// The archive, or any one of its parts. A split archive has no file at
    /// the base name at all - the parts are `name.txa.001` and so on - so a
    /// caller that has just written one has to name a part it actually wrote.
    QString archivePath;

    QString passphrase;

    /// The `.md5` file to compare against. Empty derives it from
    /// `archivePath`, which is right for a single-file archive and wrong for a
    /// split one: the sidecar is named after the set, not after part one.
    QString sidecarPath;

    /// Check every file's contents, not only that each block decompresses.
    /// The whole archive is read either way; this decides whether the entry
    /// table is checked against what is in the blocks.
    bool deep = true;

    /// Ask the system to forget its cached copy first. Without it a read-back
    /// straight after a write is served from memory and proves nothing about
    /// the drive.
    bool dropCache = true;

    /// Compare the parts against the `.md5` file beside the archive.
    bool useSidecar = true;
};

struct VerifyReport {
    bool succeeded = false;
    QString errorMessage;

    quint64 filesChecked = 0;
    quint64 filesFailed = 0;
    quint64 bytesRead = 0;
    qint64 elapsedMilliseconds = 0;

    /// False when this system has no way to evict the cache, so the read-back
    /// may have been served from memory. Said out loud rather than left for
    /// somebody to assume.
    bool cacheDropped = false;

    /// Reads that needed more than one attempt. Zero on a healthy drive.
    quint64 retriedReads = 0;

    /// Files the archive itself could not give back and a repair archive
    /// beside it could. They are not failures - the data is there - but an
    /// archive leaning on a repair is an archive on a drive that has already
    /// gone wrong once.
    quint64 filesFromRepair = 0;
    bool usedRepair = false;

    QList<VerifyFileResult> failures;
    QList<VerifyPartResult> parts;

    [[nodiscard]] bool everythingMatched() const { return succeeded && filesFailed == 0; }
};

/// Reads an archive back off the drive and checks it.
///
/// This is deliberately not `ArchiveReader::verifyAllBlocks` with a nicer face
/// on it. The question here is not "is this archive self-consistent" - it is
/// "did the drive keep what was written to it", and the difference is entirely
/// in how the reading is done: a new reader rather than the writer's own
/// buffers, the page cache dropped first where the system allows it, the
/// blocks visited in the order they lie on the medium, and every read retried
/// with the attempts counted so a stick that is failing intermittently is
/// visible rather than silently forgiven.
class VerifyService {
public:
    using ProgressCallback = std::function<void(const ProgressUpdate&)>;

    [[nodiscard]] VerifyReport run(const VerifyRequest& request, CancelToken& cancelToken,
                                   const ProgressCallback& progress = {}) const;
};

}  // namespace transmit::core
