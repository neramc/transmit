#pragma once

#include <QByteArray>
#include <QString>

#include "format/Result.h"

namespace transmit::core {

/// Copying a live SQLite database with a plain file read is the single most
/// common way to end up with a corrupt browser profile or messaging history:
/// the file may be mid-transaction, and the write-ahead log holding the newest
/// data is a separate file entirely.
///
/// These helpers use SQLite's online backup API, which takes a read lock and
/// produces a self-consistent copy with the WAL already folded in, while the
/// application keeps running.
namespace consistent_copy {

/// True when the file starts with the SQLite header. Cheap enough to run on
/// every file the capture touches.
[[nodiscard]] bool looksLikeSqlite(const QString& path);

/// Produces a consistent snapshot of a SQLite database as bytes.
[[nodiscard]] format::Result<QByteArray> readSqliteDatabase(const QString& path);

/// Reads any file, transparently using the SQLite path when the content calls
/// for it. This is what the capture pipeline calls.
[[nodiscard]] format::Result<QByteArray> readFile(const QString& path, quint64 expectedSize);

}  // namespace consistent_copy
}  // namespace transmit::core
