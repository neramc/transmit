#include <QFile>
#include <memory>

#include <sqlite3.h>

#include "core/rewrite/formats/Rewriters.h"
#include "core/utils/Logging.h"

namespace transmit::core::rewriters {
namespace {

struct SqliteCloser {
    void operator()(sqlite3* handle) const noexcept { sqlite3_close(handle); }
};
struct StatementFinalizer {
    void operator()(sqlite3_stmt* statement) const noexcept { sqlite3_finalize(statement); }
};

using Handle = std::unique_ptr<sqlite3, SqliteCloser>;
using Statement = std::unique_ptr<sqlite3_stmt, StatementFinalizer>;

/// Only a plain identifier is accepted for a table or column name. These come
/// from the recipe catalog, which a user can extend, so they are never
/// interpolated into SQL without this check.
bool isSafeIdentifier(const QString& name) {
    if (name.isEmpty() || name.size() > 64) {
        return false;
    }
    for (const QChar c : name) {
        if (!c.isLetterOrNumber() && c != u'_') {
            return false;
        }
    }
    return true;
}

}  // namespace

QList<RewriteEdit> rewriteSqlite(const QString& path, const QString& table, const QString& column,
                                 const PathTranslator& translator, const QString& appId) {
    QList<RewriteEdit> edits;

    if (!isSafeIdentifier(table) || !isSafeIdentifier(column)) {
        qCWarning(logRewrite) << "refusing an unsafe table or column name" << table << column;
        return edits;
    }
    if (!QFile::exists(path)) {
        return edits;
    }

    // The database is copied first: the plan must be reversible, and applying
    // it is a file swap like every other format.
    const QString stagedPath = path + QStringLiteral(".transmit-staged");
    QFile::remove(stagedPath);
    if (!QFile::copy(path, stagedPath)) {
        return edits;
    }

    sqlite3* rawHandle = nullptr;
    if (sqlite3_open_v2(stagedPath.toUtf8().constData(), &rawHandle, SQLITE_OPEN_READWRITE,
                        nullptr) != SQLITE_OK) {
        sqlite3_close(rawHandle);
        QFile::remove(stagedPath);
        return edits;
    }
    const Handle handle(rawHandle);

    const QByteArray selectSql =
        QStringLiteral("SELECT rowid, \"%1\" FROM \"%2\" WHERE \"%1\" IS NOT NULL")
            .arg(column, table)
            .toUtf8();

    sqlite3_stmt* rawSelect = nullptr;
    if (sqlite3_prepare_v2(handle.get(), selectSql.constData(), -1, &rawSelect, nullptr) !=
        SQLITE_OK) {
        // The table or column does not exist in this database; nothing to do.
        QFile::remove(stagedPath);
        return edits;
    }
    const Statement select(rawSelect);

    struct Update {
        sqlite3_int64 rowId = 0;
        QString oldValue;
        QString newValue;
    };
    QList<Update> updates;

    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(select.get(), 1));
        if (text == nullptr) {
            continue;
        }
        const QString original = QString::fromUtf8(text);

        int replacements = 0;
        const QString rewritten = translator.translateWithin(original, &replacements);
        if (replacements == 0 || rewritten == original) {
            continue;
        }
        updates.append({sqlite3_column_int64(select.get(), 0), original, rewritten});
    }

    if (updates.isEmpty()) {
        QFile::remove(stagedPath);
        return edits;
    }

    const QByteArray updateSql =
        QStringLiteral("UPDATE \"%1\" SET \"%2\" = ?1 WHERE rowid = ?2").arg(table, column).toUtf8();

    sqlite3_stmt* rawUpdate = nullptr;
    if (sqlite3_prepare_v2(handle.get(), updateSql.constData(), -1, &rawUpdate, nullptr) !=
        SQLITE_OK) {
        QFile::remove(stagedPath);
        return edits;
    }
    const Statement update(rawUpdate);

    sqlite3_exec(handle.get(), "BEGIN", nullptr, nullptr, nullptr);
    for (const Update& row : updates) {
        const QByteArray value = row.newValue.toUtf8();
        sqlite3_reset(update.get());
        sqlite3_bind_text(update.get(), 1, value.constData(), value.size(), SQLITE_TRANSIENT);
        sqlite3_bind_int64(update.get(), 2, row.rowId);

        if (sqlite3_step(update.get()) != SQLITE_DONE) {
            sqlite3_exec(handle.get(), "ROLLBACK", nullptr, nullptr, nullptr);
            QFile::remove(stagedPath);
            return {};
        }
        edits.append(RewriteEdit{path, QStringLiteral("%1.%2 row %3").arg(table, column).arg(row.rowId),
                                 row.oldValue, row.newValue, appId});
    }
    sqlite3_exec(handle.get(), "COMMIT", nullptr, nullptr, nullptr);

    return edits;
}

}  // namespace transmit::core::rewriters
