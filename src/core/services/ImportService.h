#pragma once

#include <QString>

#include <functional>
#include <memory>

#include "core/continuity/ContinuityTypes.h"
#include "core/services/ScanService.h"
#include "format/Container.h"
#include "platform/PlatformService.h"

namespace transmit::core {

/// What `inspect` shows before anything is restored: where the archive came
/// from, what it holds, and whether it needs a passphrase.
struct ArchiveSummary {
    bool valid = false;
    QString errorMessage;

    QString archiveId;
    QString label;
    bool encrypted = false;
    bool unlocked = false;
    int partCount = 0;

    OsFamily sourceOs = OsFamily::Unknown;
    QString sourceOsName;
    QString sourceHost;
    QString sourceUser;
    QDateTime capturedAt;
    QString writtenByVersion;

    quint64 fileCount = 0;
    quint64 rawBytes = 0;
    QMap<int, quint64> filesPerDomain;
    QMap<int, quint64> bytesPerDomain;
};

/// A restore of this archive into this place that stopped part way.
///
/// What the window needs to offer to finish one, without having to work out
/// for itself where the record is kept - the service that wrote it is the
/// only thing that should have to know that.
struct InterruptedRestore {
    bool found = false;

    /// How many items the interrupted run had already put in place. This is
    /// what makes the offer worth showing: "carry on" over four files is not
    /// worth a decision, over four thousand it is.
    quint64 itemsAlreadyInPlace = 0;

    /// The undo point that run made, if it made one. An interrupted restore
    /// is exactly the one somebody may want reversed instead of finished.
    QString rollbackArchivePath;
};

/// Restores an archive onto this machine, translating everything the target OS
/// needs translated.
class ImportService {
public:
    using ProgressCallback = std::function<void(const ProgressUpdate&)>;

    explicit ImportService(platform::PlatformService& platformService);

    /// Reads the header and, when possible, the manifest. Safe to call on an
    /// encrypted archive without a passphrase: what can be read is returned and
    /// `encrypted` is set.
    [[nodiscard]] ArchiveSummary inspect(const QString& archivePath,
                                         const QString& passphrase = {}) const;

    [[nodiscard]] ImportReport run(const ImportRequest& request, CancelToken& cancelToken,
                                   const ProgressCallback& progress = {});

    /// Looks for a restore of this archive into this place that stopped part
    /// way, so the window can offer to finish it.
    ///
    /// Exposed rather than left for the caller to find, because where the
    /// record lives is derived from the destination, the home directory and
    /// the archive's own identifier, and a second answer to that would be a
    /// window offering to carry on with a record the restore will not read.
    /// Nothing here reports a problem: an unreadable or absent record simply
    /// means there is nothing to offer.
    [[nodiscard]] InterruptedRestore findInterruptedRestore(
        const QString& archivePath, const QString& destinationOverride) const;

private:
    /// The known-folder table to restore into: this machine's, or a synthetic
    /// one when the caller is emulating another OS or restoring to a folder.
    [[nodiscard]] format::PathTokenMap targetTokens(const ImportRequest& request) const;

    /// Where the undo point and the record of the run are kept.
    [[nodiscard]] QString stateDirectoryFor(const QString& destinationOverride) const;

    /// Works out which settings a real restore would repoint, without touching
    /// anything the user owns.
    ///
    /// A preview that could not answer this would be missing the most invasive
    /// part of the operation, so the application state is unpacked into a
    /// throwaway directory, planned against, and thrown away again.
    void previewRewrites(format::ArchiveReader& reader, const format::Manifest& manifest,
                         const ImportRequest& request, const format::PathTokenMap& targetFolders,
                         OsFamily targetOs, ImportReport& report) const;

    platform::PlatformService& platform_;
};

}  // namespace transmit::core
