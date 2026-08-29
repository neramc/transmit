#include "core/services/VerifyService.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"
#include "format/ChecksumSidecar.h"
#include "format/Container.h"
#include "format/FileIo.h"
#include "format/hash/ContentHash.h"

namespace transmit::core {
namespace {

/// How much of each end of a part is read twice.
///
/// A drive that acknowledged a write and then dropped it usually loses whole
/// erase blocks, and the first and last of a part are where a truncated or
/// half-committed write shows. Reading them again after everything else means
/// the second read comes from a different point in time - which is the only
/// way a cache that outlived the eviction gives itself away.
constexpr qint64 kEndSampleSize = 4096;

VerifyReport fail(VerifyReport report, const QString& message) {
    report.succeeded = false;
    report.errorMessage = message;
    return report;
}

/// The first and last few kilobytes of a file, read directly.
format::Result<std::pair<format::ByteBuffer, format::ByteBuffer>> readEnds(
    const std::filesystem::path& path) {
    TRANSMIT_TRY(stream, format::FileStream::open(path, format::FileStream::Mode::Read));
    TRANSMIT_TRY(size, stream.size());

    const auto take =
        static_cast<std::size_t>(std::min<qint64>(kEndSampleSize, static_cast<qint64>(size)));

    format::ByteBuffer head(take);
    TRANSMIT_CHECK(stream.seek(0));
    TRANSMIT_CHECK(stream.read(format::MutableByteView(head.data(), head.size())));

    format::ByteBuffer tail(take);
    TRANSMIT_CHECK(stream.seek(size - take));
    TRANSMIT_CHECK(stream.read(format::MutableByteView(tail.data(), tail.size())));

    return std::pair{std::move(head), std::move(tail)};
}

}  // namespace

QString verifyStatusName(VerifyStatus status) {
    switch (status) {
        case VerifyStatus::Ok:
            return QCoreApplication::translate("Verify", "matched");
        case VerifyStatus::ContentMismatch:
            return QCoreApplication::translate("Verify", "came back different");
        case VerifyStatus::Md5Mismatch:
            return QCoreApplication::translate("Verify", "MD5 did not match");
        case VerifyStatus::Unreadable:
            return QCoreApplication::translate("Verify", "could not be read");
    }
    return {};
}

VerifyService::VerifyService(const platform::PlatformService& platform) : platform_(platform) {}

VerifyReport VerifyService::run(const VerifyRequest& request, CancelToken& cancelToken,
                                const ProgressCallback& progress) const {
    VerifyReport report;
    QElapsedTimer timer;
    timer.start();

    const std::filesystem::path archive = format::toFsPath(toUtf8(request.archivePath));

    // Opened once to learn which parts there are, before anything is read for
    // real - the cache has to be dropped on all of them first, and dropping it
    // on a file being read through would be pointless.
    std::vector<std::filesystem::path> parts;
    {
        auto opening = format::ArchiveReader::open(archive);
        if (!opening) {
            return fail(std::move(report), describeError(opening.error()));
        }
        parts = (*opening)->parts();
    }

    if (request.dropCache) {
        // All or nothing: if one part could not be evicted the read-back may
        // be served from memory, and a report that said "cache dropped" would
        // be claiming more than was done.
        bool dropped = !parts.empty();
        for (const std::filesystem::path& part : parts) {
            const auto answer = format::dropFromPageCache(part);
            if (!answer || !*answer) {
                dropped = false;
            }
        }
        report.cacheDropped = dropped;
        if (!dropped) {
            qCInfo(logCapture) << "could not drop the page cache: the read-back may be served "
                                  "from memory";
        }
    }

    // A second reader, opened after the eviction. Reusing the first would read
    // through buffers the eviction was meant to get rid of.
    auto opening = format::ArchiveReader::open(archive);
    if (!opening) {
        return fail(std::move(report), describeError(opening.error()));
    }
    auto reader = std::move(opening).value();

    if (reader->isEncrypted()) {
        if (request.passphrase.isEmpty()) {
            return fail(std::move(report),
                        QCoreApplication::translate(
                            "Verify", "This archive is encrypted and no passphrase was given."));
        }
        if (const auto unlocked = reader->unlock(toUtf8(request.passphrase)); !unlocked) {
            return fail(std::move(report), describeError(unlocked.error()));
        }
    }

    const auto manifest = reader->manifest();
    if (!manifest) {
        return fail(std::move(report), describeError(manifest.error()));
    }

    // Blocks in the order they lie on the medium rather than the order they
    // were numbered. On a stick that is the difference between one sweep and
    // several thousand seeks.
    std::vector<const format::BlockRecord*> blocks;
    blocks.reserve((*manifest)->blocks.size());
    for (const format::BlockRecord& block : (*manifest)->blocks) {
        blocks.push_back(&block);
    }
    std::sort(blocks.begin(), blocks.end(),
              [](const format::BlockRecord* a, const format::BlockRecord* b) {
                  return a->streamOffset < b->streamOffset;
              });

    std::unordered_map<std::uint32_t, std::vector<const format::ManifestEntry*>> byBlock;
    quint64 filesToCheck = 0;
    for (const format::ManifestEntry& entry : (*manifest)->entries) {
        if (entry.hasContent()) {
            byBlock[entry.location.blockId].push_back(&entry);
            ++filesToCheck;
        }
    }

    ProgressUpdate update;
    update.phase = ProgressPhase::Verifying;
    update.filesTotal = filesToCheck;
    update.stage = QCoreApplication::translate("Verify", "Reading the archive back");

    for (const format::BlockRecord* block : blocks) {
        if (cancelToken.isCancelled()) {
            return fail(std::move(report), QCoreApplication::translate("Verify", "Cancelled."));
        }

        // The retrying happens inside VolumeSource, which is the layer that
        // knows a read from a part from a read of anything else. Counting here
        // as well would count nothing: by the time readBlock returns, the
        // second attempt has already succeeded and the failure is invisible.
        const auto before = reader->retriedReads();
        const auto loaded = reader->readBlock(block->blockId);
        const int attempts = static_cast<int>(reader->retriedReads() - before) + 1;

        const auto found = byBlock.find(block->blockId);
        const std::vector<const format::ManifestEntry*>& entries =
            found == byBlock.end() ? std::vector<const format::ManifestEntry*>{} : found->second;

        if (!loaded) {
            // The block would not come back, so every file in it is lost with
            // it. Recorded one by one, because the person reading this wants
            // to know which of their files it was.
            for (const format::ManifestEntry* entry : entries) {
                VerifyFileResult result;
                result.path = fromUtf8(entry->path.toDisplayString());
                result.appId = fromUtf8(entry->appId);
                result.size = entry->size;
                result.status = VerifyStatus::Unreadable;
                result.attempts = attempts;
                result.detail = describeError(loaded.error());
                report.failures.push_back(result);
                ++report.filesFailed;
                ++report.filesChecked;
            }
            continue;
        }

        report.bytesRead += block->rawSize;

        for (const format::ManifestEntry* entry : entries) {
            ++report.filesChecked;

            VerifyFileResult result;
            result.path = fromUtf8(entry->path.toDisplayString());
            result.appId = fromUtf8(entry->appId);
            result.size = entry->size;
            result.attempts = attempts;

            const std::uint64_t end = entry->location.offset + entry->location.length;
            if (end > loaded->size()) {
                result.status = VerifyStatus::ContentMismatch;
                result.detail =
                    QCoreApplication::translate("Verify", "It points outside the block it is in.");
                report.failures.push_back(result);
                ++report.filesFailed;
                continue;
            }

            if (request.deep) {
                const format::ByteView slice =
                    loaded->subspan(static_cast<std::size_t>(entry->location.offset),
                                    static_cast<std::size_t>(entry->location.length));
                const format::ContentDigests digests = format::hashContent(slice, entry->hasMd5());

                if (digests.blake2b != entry->contentHash) {
                    result.status = VerifyStatus::ContentMismatch;
                    result.detail = QCoreApplication::translate(
                        "Verify", "The bytes on the drive are not the bytes that were captured.");
                    report.failures.push_back(result);
                    ++report.filesFailed;
                    continue;
                }
                if (entry->hasMd5() && digests.md5 != entry->contentMd5) {
                    // Both hashes are over the same bytes, so this cannot
                    // happen to data that is merely damaged: one of the two
                    // recorded digests is wrong, which means the manifest is.
                    result.status = VerifyStatus::Md5Mismatch;
                    result.detail = QCoreApplication::translate(
                        "Verify",
                        "The content hash matched and the MD5 did not, so the archive's own "
                        "record of this file disagrees with itself.");
                    report.failures.push_back(result);
                    ++report.filesFailed;
                    continue;
                }
            }

            update.filesDone = report.filesChecked;
            update.bytesDone = report.bytesRead;
            update.currentItem = result.path;
            if (progress) {
                progress(update);
            }
        }
    }

    // Every read this reader made, including opening the archive and loading
    // the manifest. A drive that had to be asked twice for the footer is the
    // same drive, and the count is about the drive.
    report.retriedReads = reader->retriedReads();

    // ------------------------------------------------------------- parts
    for (const std::filesystem::path& part : parts) {
        VerifyPartResult partResult;
        partResult.path = fromUtf8(format::fromFsPath(part));

        std::error_code ec;
        partResult.size = std::filesystem::file_size(part, ec);

        // Read the ends a second time, after everything else has been read
        // through. A cache that survived the eviction would answer both reads
        // from the same copy; a drive that is actually returning what it holds
        // has had to go back to it.
        const auto first = readEnds(part);
        const auto second = readEnds(part);
        if (!first || !second) {
            partResult.endsMatched = false;
            partResult.detail = describeError(first ? second.error() : first.error());
        } else {
            partResult.endsMatched =
                first->first == second->first && first->second == second->second;
            if (!partResult.endsMatched) {
                partResult.detail = QCoreApplication::translate(
                    "Verify", "The same bytes read twice came back differently.");
            }
        }
        report.parts.push_back(partResult);
    }

    if (request.useSidecar) {
        const QString sidecarName = request.sidecarPath.isEmpty()
                                        ? request.archivePath + QStringLiteral(".md5")
                                        : request.sidecarPath;
        const std::filesystem::path sidecarPath = format::toFsPath(toUtf8(sidecarName));
        if (std::filesystem::exists(sidecarPath)) {
            const auto listed = format::readChecksumSidecar(sidecarPath);
            if (listed) {
                for (const format::SidecarPart& expected : *listed) {
                    const auto here = std::find_if(
                        parts.begin(), parts.end(), [&expected](const std::filesystem::path& part) {
                            return part.filename().string() == expected.fileName;
                        });
                    if (here == parts.end()) {
                        continue;
                    }
                    const auto actual = format::md5OfFile(*here);
                    const bool matched = actual && *actual == expected.md5;

                    const QString path = fromUtf8(format::fromFsPath(*here));
                    for (VerifyPartResult& partResult : report.parts) {
                        if (partResult.path != path) {
                            continue;
                        }
                        partResult.md5Matched = matched;
                        if (!matched && partResult.detail.isEmpty()) {
                            partResult.detail = QCoreApplication::translate(
                                "Verify", "It does not match the checksum written beside it.");
                        }
                    }
                }
            }
        }
    }

    for (const VerifyPartResult& partResult : report.parts) {
        if (!partResult.endsMatched || !partResult.md5Matched) {
            report.elapsedMilliseconds = timer.elapsed();
            return fail(std::move(report), QCoreApplication::translate("Verify", "%1: %2")
                                               .arg(partResult.path, partResult.detail));
        }
    }

    // A run that read the whole archive and found two files wrong did not
    // succeed, however smoothly it got to the end. Reaching this line means
    // nothing went wrong with the *reading*; whether the archive is sound is a
    // different question and it is answered by the count.
    report.succeeded = report.filesFailed == 0;
    if (!report.succeeded && report.errorMessage.isEmpty()) {
        report.errorMessage =
            QCoreApplication::translate("Verify",
                                        "%1 of %2 file(s) did not come back off the drive "
                                        "unchanged.")
                .arg(report.filesFailed)
                .arg(report.filesChecked);
    }
    report.elapsedMilliseconds = timer.elapsed();

    qCInfo(logCapture) << "verified" << report.filesChecked << "files," << report.filesFailed
                       << "failed," << report.retriedReads << "reads retried in"
                       << report.elapsedMilliseconds << "ms";
    return report;
}

}  // namespace transmit::core
