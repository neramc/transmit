#pragma once

#include <QString>
#include <functional>

#include "core/continuity/ContinuityTypes.h"
#include "core/services/ScanService.h"
#include "platform/PlatformService.h"

namespace transmit::core {

/// Turns a CaptureSelection into an archive on the target medium.
///
/// The whole call is synchronous and blocking; ExportJob runs it on a worker
/// thread and forwards its progress to the UI. Keeping the service itself free
/// of threading makes it directly testable.
class ExportService {
public:
    using ProgressCallback = std::function<void(const ProgressUpdate&)>;

    explicit ExportService(platform::PlatformService& platformService);

    [[nodiscard]] ExportReport run(const ExportRequest& request, CancelToken& cancelToken,
                                   const ProgressCallback& progress = {});

    /// Estimates the uncompressed size of a selection so the UI can warn about
    /// a target volume that is too small before anything is written.
    [[nodiscard]] quint64 estimateSize(const CaptureSelection& selection,
                                       CancelToken& cancelToken) const;

    /// Chooses a split size for a target volume: FAT32 forces splitting, and a
    /// volume that cannot hold the whole archive is reported by the caller.
    [[nodiscard]] static quint64 splitSizeFor(const platform::StorageVolume& volume);

private:
    platform::PlatformService& platform_;
};

}  // namespace transmit::core
