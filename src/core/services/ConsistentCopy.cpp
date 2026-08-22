#include "core/services/ConsistentCopy.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <sqlite3.h>

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"

namespace transmit::core::consistent_copy {
namespace {

constexpr char kSqliteMagic[] = "SQLite format 3";
constexpr int kSqliteMagicLength = 15;

/// Steps the backup in chunks so a very large database does not hold the
/// source locked for one long operation.
constexpr int kBackupPageStep = 512;

struct SqliteCloser {
    void operator()(sqlite3* handle) const noexcept { sqlite3_close(handle); }
};
using SqliteHandle = std::unique_ptr<sqlite3, SqliteCloser>;

format::Error sqliteError(sqlite3* handle, const char* what) {
    const char* message = handle != nullptr ? sqlite3_errmsg(handle) : "unknown error";
    return format::makeError(format::ErrorCode::IoError, what, ": ", message);
}

}  // namespace

bool looksLikeSqlite(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray header = file.read(kSqliteMagicLength);
    return header.size() == kSqliteMagicLength &&
           std::memcmp(header.constData(), kSqliteMagic, kSqliteMagicLength) == 0;
}

format::Result<QByteArray> readSqliteDatabase(const QString& path) {
    QTemporaryDir workspace;
    if (!workspace.isValid()) {
        return format::makeError(format::ErrorCode::IoError,
                                 "could not create a temporary directory for a database copy");
    }
    const QString destinationPath = workspace.filePath(QStringLiteral("copy.sqlite"));

    sqlite3* rawSource = nullptr;
    // Read-only, and the URI form so a database with an unusual name still opens.
    if (sqlite3_open_v2(path.toUtf8().constData(), &rawSource, SQLITE_OPEN_READONLY, nullptr) !=
        SQLITE_OK) {
        const auto error = sqliteError(rawSource, "could not open the database");
        sqlite3_close(rawSource);
        return error;
    }
    const SqliteHandle source(rawSource);

    sqlite3* rawDestination = nullptr;
    if (sqlite3_open_v2(destinationPath.toUtf8().constData(), &rawDestination,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        const auto error = sqliteError(rawDestination, "could not create the database copy");
        sqlite3_close(rawDestination);
        return error;
    }
    const SqliteHandle destination(rawDestination);

    sqlite3_backup* backup =
        sqlite3_backup_init(destination.get(), "main", source.get(), "main");
    if (backup == nullptr) {
        return sqliteError(destination.get(), "could not start the database copy");
    }

    int rc = SQLITE_OK;
    do {
        rc = sqlite3_backup_step(backup, kBackupPageStep);
        if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
            // The application is mid-write; wait briefly and try again rather
            // than giving up on the file.
            sqlite3_sleep(50);
        }
    } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);

    const int finishResult = sqlite3_backup_finish(backup);
    if (rc != SQLITE_DONE) {
        return sqliteError(destination.get(), "the database copy did not complete");
    }
    if (finishResult != SQLITE_OK) {
        return sqliteError(destination.get(), "the database copy could not be finalised");
    }

    QFile copy(destinationPath);
    if (!copy.open(QIODevice::ReadOnly)) {
        return format::makeError(format::ErrorCode::IoError,
                                 "could not read back the database copy");
    }
    return copy.readAll();
}

format::Result<QByteArray> readFile(const QString& path, quint64 expectedSize) {
    if (looksLikeSqlite(path)) {
        auto consistent = readSqliteDatabase(path);
        if (consistent) {
            return consistent;
        }
        // A database that cannot be opened - encrypted, an unsupported format,
        // or a name that only looks like SQLite - still gets copied as bytes.
        qCDebug(logCapture) << "falling back to a plain read for" << path << ":"
                            << describeError(consistent.error());
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return format::makeError(format::ErrorCode::PermissionDenied,
                                 "could not open '", toUtf8(path), "': ",
                                 toUtf8(file.errorString()));
    }

    QByteArray content = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        return format::makeError(format::ErrorCode::IoError, "could not read '", toUtf8(path),
                                 "': ", toUtf8(file.errorString()));
    }
    Q_UNUSED(expectedSize);
    return content;
}

}  // namespace transmit::core::consistent_copy
