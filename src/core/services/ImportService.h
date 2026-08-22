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

private:
    /// The known-folder table to restore into: this machine's, or a synthetic
    /// one when the caller is emulating another OS or restoring to a folder.
    [[nodiscard]] format::PathTokenMap targetTokens(const ImportRequest& request) const;

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
