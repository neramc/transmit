#pragma once

#include <QList>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <atomic>
#include <functional>

#include "core/continuity/ContinuityTypes.h"
#include "format/Manifest.h"
#include "platform/PlatformService.h"

namespace transmit::core {

/// One filesystem object found by a scan, already carrying everything the
/// manifest needs so the capture pass does not have to stat it again.
struct ScannedItem {
    QString absolutePath;
    format::TokenizedPath tokenPath;
    format::EntryType type = format::EntryType::File;
    DomainId domain = DomainId::UserData;
    QString appId;
    quint64 size = 0;
    qint64 modifiedUnixNs = 0;
    qint64 createdUnixNs = 0;
    format::PosixMetadata posix;
    format::WindowsMetadata windows;
    QString symlinkTarget;

    /// Set when the scan could not read the item; it still appears in the
    /// report so nothing disappears silently.
    QString problem;
};

struct ScanResult {
    QList<ScannedItem> items;
    quint64 totalBytes = 0;
    quint64 fileCount = 0;
    quint64 directoryCount = 0;
    quint64 symlinkCount = 0;
    quint64 skippedCount = 0;
    QList<ContinuityNote> notes;
};

/// Cooperative cancellation shared between the UI and a running job.
class CancelToken {
public:
    void cancel() noexcept { cancelled_.store(true, std::memory_order_relaxed); }
    void reset() noexcept { cancelled_.store(false, std::memory_order_relaxed); }
    [[nodiscard]] bool isCancelled() const noexcept {
        return cancelled_.load(std::memory_order_relaxed);
    }

private:
    std::atomic_bool cancelled_{false};
};

/// Compiles the wildcard exclusion patterns once and matches relative paths
/// against them. Patterns use "**" for "any number of directories".
class ExcludeMatcher {
public:
    explicit ExcludeMatcher(const QStringList& patterns = {});

    void add(const QStringList& patterns);
    [[nodiscard]] bool matches(const QString& relativePath) const;
    [[nodiscard]] bool isEmpty() const noexcept { return patterns_.isEmpty(); }

private:
    QList<QRegularExpression> patterns_;
};

/// Walks a CaptureSelection and produces the item list the capture works from.
/// Kept separate from the capture so the UI can show sizes and counts, and let
/// the user adjust the selection, before anything is written.
class ScanService {
public:
    using ProgressCallback = std::function<void(const ProgressUpdate&)>;

    explicit ScanService(const platform::PlatformService& platformService);

    [[nodiscard]] ScanResult scan(const CaptureSelection& selection, CancelToken& cancelToken,
                                  const ProgressCallback& progress = {}) const;

private:
    void scanRoot(const CaptureRoot& root, const CaptureSelection& selection,
                  const ExcludeMatcher& globalExcludes, ScanResult& result,
                  CancelToken& cancelToken, const ProgressCallback& progress) const;

    const platform::PlatformService& platform_;
    format::PathTokenMap tokens_;
};

}  // namespace transmit::core
