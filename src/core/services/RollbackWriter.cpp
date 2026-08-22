#include "core/services/RollbackWriter.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"
#include "format/BlockPacker.h"
#include "format/Container.h"
#include "format/Serialization.h"

namespace transmit::core {
namespace {

/// Rollback archives are read back on the same machine that wrote them, so the
/// fast preset is right: the time saved matters more than the space, and the
/// archive is usually deleted once the user is happy.
constexpr auto kRollbackPreset = format::CompressionPreset::Fast;

format::ByteBuffer encodeCreatedPaths(const QStringList& created) {
    format::ByteBuffer buffer;
    format::ByteWriter writer(buffer);
    for (const QString& path : created) {
        writer.putString(1, toUtf8(path));
    }
    return buffer;
}

QStringList decodeCreatedPaths(format::ByteView data) {
    QStringList created;
    format::ByteReader reader(data);
    while (!reader.atEnd()) {
        const auto tag = reader.getTag();
        if (!tag) {
            break;
        }
        if (tag->field == 1) {
            if (const auto value = reader.getString()) {
                created << fromUtf8(*value);
            } else {
                break;
            }
        } else if (!reader.skip(tag->type)) {
            break;
        }
    }
    return created;
}

}  // namespace

format::Result<QString> RollbackWriter::capture(const QStringList& targets,
                                                const QString& directory) {
    QStringList existing;
    QStringList created;

    for (const QString& path : targets) {
        if (QFileInfo::exists(path)) {
            if (QFileInfo(path).isFile()) {
                existing << path;
            }
        } else {
            created << path;
        }
    }

    if (existing.isEmpty() && created.isEmpty()) {
        return QString();
    }

    const QString rollbackDirectory = QDir(directory).filePath(QLatin1String(kDirectoryName));
    QDir().mkpath(rollbackDirectory);

    const QString archivePath =
        QDir(rollbackDirectory)
            .filePath(
                QStringLiteral("rollback-%1.txa")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"))));

    format::ArchiveOptions options;
    options.preset = kRollbackPreset;

    auto writerResult =
        format::ArchiveWriter::create(format::toFsPath(toUtf8(archivePath)), options);
    if (!writerResult) {
        return std::move(writerResult).error();
    }
    auto writer = std::move(writerResult).value();

    format::BlockPacker packer(options.solidBlockSize,
                               [&writer](format::ByteView raw) -> format::Result<quint32> {
                                   const quint32 blockId = writer->nextBlockId();
                                   TRANSMIT_TRY(prepared, writer->prepare(blockId, raw));
                                   TRANSMIT_CHECK(writer->writePrepared(prepared));
                                   return blockId;
                               });

    format::Manifest manifest;
    manifest.label = toUtf8(QStringLiteral("Undo point"));
    manifest.source.os = format::hostOsFamily();
    manifest.source.capturedUnix = QDateTime::currentSecsSinceEpoch();

    QList<format::BlockPacker::PlacementId> placements;
    quint64 nextId = 1;

    for (const QString& path : existing) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            // A file we cannot read is one we could not have overwritten
            // either, so leaving it out costs nothing.
            qCDebug(logRestore) << "not backing up unreadable" << path;
            continue;
        }
        const QByteArray content = file.readAll();

        format::ManifestEntry entry;
        entry.id = nextId++;
        entry.domain = format::DomainId::UserData;
        entry.type = format::EntryType::File;
        // Absolute, because a rollback goes back exactly where it came from.
        entry.path =
            format::TokenizedPath{format::PathTokenId::Absolute,
                                  format::normalizePath(toUtf8(path), format::hostOsFamily())};
        entry.size = static_cast<quint64>(content.size());
        entry.modifiedUnixNs = QFileInfo(path).lastModified().toMSecsSinceEpoch() * 1000000LL;
        entry.contentHash = format::Blake2b::hash256(toByteView(content));

        auto handle = packer.add(entry.contentHash, toByteView(content));
        if (!handle) {
            return std::move(handle).error();
        }
        placements.push_back(*handle);
        manifest.entries.push_back(std::move(entry));
    }

    if (const auto status = packer.flush(); !status) {
        return std::move(status).error();
    }

    for (qsizetype i = 0; i < placements.size(); ++i) {
        auto location = packer.location(placements[i]);
        if (!location) {
            return std::move(location).error();
        }
        manifest.entries[static_cast<std::size_t>(i)].location = *location;
    }

    if (!created.isEmpty()) {
        manifest.payloads.push_back(format::DomainPayload{
            format::DomainId::UserData, kCreatedPayloadKind, encodeCreatedPaths(created)});
    }

    if (const auto status = writer->finish(manifest); !status) {
        return std::move(status).error();
    }

    qCInfo(logRestore) << "wrote an undo point covering" << manifest.entries.size()
                       << "existing files and" << created.size() << "that will be created";
    return archivePath;
}

format::Result<RollbackWriter::UndoResult> RollbackWriter::undo(const QString& archivePath) {
    UndoResult result;

    auto readerResult = format::ArchiveReader::open(format::toFsPath(toUtf8(archivePath)));
    if (!readerResult) {
        return std::move(readerResult).error();
    }
    auto reader = std::move(readerResult).value();

    auto manifestResult = reader->manifest();
    if (!manifestResult) {
        return std::move(manifestResult).error();
    }
    const format::Manifest& manifest = **manifestResult;

    // Files that existed before go back exactly as they were.
    for (const format::ManifestEntry& entry : manifest.entries) {
        if (entry.type != format::EntryType::File) {
            continue;
        }
        const QString path = fromUtf8(entry.path.relative);

        auto content = reader->readEntry(entry);
        if (!content) {
            result.errors << QStringLiteral("%1: %2").arg(path, describeError(content.error()));
            continue;
        }

        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            result.errors << QStringLiteral("%1: %2").arg(path, file.errorString());
            continue;
        }
        file.write(reinterpret_cast<const char*>(content->data()),
                   static_cast<qint64>(content->size()));
        if (file.commit()) {
            ++result.filesRestored;
        } else {
            result.errors << QStringLiteral("%1: %2").arg(path, file.errorString());
        }
    }

    // Files the restore created did not exist before, so putting things back
    // means removing them.
    if (const auto* payload =
            manifest.findPayload(format::DomainId::UserData, kCreatedPayloadKind)) {
        for (const QString& path : decodeCreatedPaths(payload->data)) {
            if (!QFileInfo::exists(path)) {
                continue;
            }
            if (QFile::remove(path)) {
                ++result.filesRemoved;
            } else {
                result.errors << QStringLiteral("could not remove %1").arg(path);
            }
        }
    }

    qCInfo(logRestore) << "undo put back" << result.filesRestored << "files and removed"
                       << result.filesRemoved;
    return result;
}

}  // namespace transmit::core
