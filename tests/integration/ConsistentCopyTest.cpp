#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <sqlite3.h>

#include "core/services/ConsistentCopy.h"
#include "core/utils/Conversions.h"

namespace transmit::core {

/// Copying a file that something else is still writing to.
///
/// Two failures live here and neither announces itself. A browser profile or a
/// message history copied byte for byte while its application is running is a
/// database mid-transaction, with the newest data in a separate file that a
/// plain copy does not take - so the archive holds a database that opens and
/// is missing yesterday. And a file that shrinks between being counted and
/// being read is stored short, with a hash that matches the short version,
/// which makes it a file nothing will ever say is wrong.
class ConsistentCopyTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void tellsADatabaseFromAFileThatIsNotOne();
    void aDatabaseComesBackAsADatabase();
    void whatIsOnlyInTheWriteAheadLogComesToo();
    void aFileThatOnlyLooksLikeADatabaseIsCopiedAsBytes();
    void anOrdinaryFileIsReadAsItIs();
    void aFileThatShrankWhileItWasBeingReadIsRefused();
    void aFileThatGrewIsFine();
    void withNoExpectedSizeNothingIsChecked();
    void whatCannotBeReadIsSaidRatherThanReturnedEmpty();

private:
    [[nodiscard]] QString path(const QString& name) const { return workspace_.filePath(name); }

    /// A database with one table and `rows` rows in it, left closed.
    [[nodiscard]] QString makeDatabase(const QString& name, int rows, bool writeAheadLog = false);

    /// The rows a database holds, read from bytes rather than from a path, so
    /// what the copy produced can be asked directly. -1 when the bytes are not
    /// a database this machine will open.
    [[nodiscard]] int rowsIn(const QString& name, const QByteArray& database) const;

    QTemporaryDir workspace_;
};

void ConsistentCopyTest::initTestCase() {
    QVERIFY(workspace_.isValid());
}

QString ConsistentCopyTest::makeDatabase(const QString& name, int rows, bool writeAheadLog) {
    const QString file = path(name);
    sqlite3* handle = nullptr;
    if (sqlite3_open(file.toUtf8().constData(), &handle) != SQLITE_OK) {
        sqlite3_close(handle);
        return {};
    }
    if (writeAheadLog) {
        sqlite3_exec(handle, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
    }
    sqlite3_exec(handle, "CREATE TABLE notes(id INTEGER PRIMARY KEY, body TEXT)", nullptr, nullptr,
                 nullptr);
    for (int i = 0; i < rows; ++i) {
        const QByteArray insert =
            QStringLiteral("INSERT INTO notes(body) VALUES('note %1')").arg(i).toUtf8();
        sqlite3_exec(handle, insert.constData(), nullptr, nullptr, nullptr);
    }
    sqlite3_close(handle);
    return file;
}

int ConsistentCopyTest::rowsIn(const QString& name, const QByteArray& database) const {
    const QString scratch = path(name);
    {
        QFile file(scratch);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return -1;
        }
        file.write(database);
    }

    // Read-write, deliberately. A database produced by the backup API keeps
    // the write-ahead journal mode of the one it came from, and opening a
    // database marked that way read-only needs a shared-memory file beside it
    // that a read-only connection may not be allowed to create - which Linux
    // lets pass and macOS and Windows do not. Nothing here is being protected
    // from writes; the file is a copy of a copy in a temporary directory.
    sqlite3* handle = nullptr;
    if (sqlite3_open_v2(scratch.toUtf8().constData(), &handle,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        sqlite3_close(handle);
        return -1;
    }
    sqlite3_stmt* statement = nullptr;
    int count = -1;
    if (sqlite3_prepare_v2(handle, "SELECT COUNT(*) FROM notes", -1, &statement, nullptr) ==
        SQLITE_OK) {
        if (sqlite3_step(statement) == SQLITE_ROW) {
            count = sqlite3_column_int(statement, 0);
        }
    }
    sqlite3_finalize(statement);
    sqlite3_close(handle);
    return count;
}

void ConsistentCopyTest::tellsADatabaseFromAFileThatIsNotOne() {
    QVERIFY(consistent_copy::looksLikeSqlite(makeDatabase(QStringLiteral("real.sqlite"), 3)));

    const auto write = [this](const QString& name, const QByteArray& bytes) -> QString {
        QFile file(path(name));
        if (!file.open(QIODevice::WriteOnly)) {
            return {};
        }
        file.write(bytes);
        return path(name);
    };

    QVERIFY(!consistent_copy::looksLikeSqlite(write(QStringLiteral("plain.txt"), "hello there")));
    QVERIFY(!consistent_copy::looksLikeSqlite(write(QStringLiteral("empty.txt"), QByteArray())));

    // Shorter than the magic. Reading fifteen bytes out of a nine-byte file
    // has to be a no rather than whatever was after it in memory.
    QVERIFY(!consistent_copy::looksLikeSqlite(write(QStringLiteral("short.txt"), "SQLite f")));
    QVERIFY(!consistent_copy::looksLikeSqlite(path(QStringLiteral("not-there.sqlite"))));
}

void ConsistentCopyTest::aDatabaseComesBackAsADatabase() {
    const QString source = makeDatabase(QStringLiteral("notes.sqlite"), 12);
    QVERIFY(!source.isEmpty());

    const auto copied = consistent_copy::readSqliteDatabase(source);
    QVERIFY2(copied.operator bool(), "the copy failed");
    QVERIFY(!copied->isEmpty());

    // Not "it produced some bytes": the bytes have to be a database, and the
    // database has to hold what the original held.
    QCOMPARE(rowsIn(QStringLiteral("read-back.sqlite"), *copied), 12);
}

void ConsistentCopyTest::whatIsOnlyInTheWriteAheadLogComesToo() {
    // The reason this code exists. In write-ahead logging mode the newest rows
    // are in a file beside the database, and a copy that takes only the
    // database file gets a profile that opens and is missing the last however
    // long. The writer is deliberately still open: closing it checkpoints, and
    // then there would be nothing left to get wrong.
    const QString file = path(QStringLiteral("live.sqlite"));
    sqlite3* writer = nullptr;
    QVERIFY2(sqlite3_open(file.toUtf8().constData(), &writer) == SQLITE_OK, sqlite3_errmsg(writer));
    sqlite3_exec(writer, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
    sqlite3_exec(writer, "CREATE TABLE notes(id INTEGER PRIMARY KEY, body TEXT)", nullptr, nullptr,
                 nullptr);
    for (int i = 0; i < 40; ++i) {
        sqlite3_exec(writer, "INSERT INTO notes(body) VALUES('while you were reading')", nullptr,
                     nullptr, nullptr);
    }

    const auto copied = consistent_copy::readFile(file, 0);
    QVERIFY2(copied.operator bool(), "the copy of a live database failed");
    const int carried = rowsIn(QStringLiteral("carried.sqlite"), *copied);

    // And the plain read, for the comparison that makes the point.
    QByteArray raw;
    {
        QFile plain(file);
        QVERIFY(plain.open(QIODevice::ReadOnly));
        raw = plain.readAll();
    }
    const int plainly = rowsIn(QStringLiteral("plainly.sqlite"), raw);

    sqlite3_close(writer);

    QVERIFY2(carried == 40,
             qPrintable(QStringLiteral("the copy holds %1 of the 40 rows; the plain read holds %2")
                            .arg(carried)
                            .arg(plainly)));
    QVERIFY2(plainly < 40, qPrintable(QStringLiteral("a plain read of this database already had "
                                                     "all %1 rows, so it proves nothing")
                                          .arg(plainly)));
}

void ConsistentCopyTest::aFileThatOnlyLooksLikeADatabaseIsCopiedAsBytes() {
    // Something that starts with the magic and is not a database: an encrypted
    // profile, a format SQLite does not know, a file somebody named that way.
    // It still has to travel, as the bytes it is.
    const QByteArray pretend = QByteArray("SQLite format 3") + QByteArray(200, '\x7F');
    const QString file = path(QStringLiteral("pretend.sqlite"));
    {
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(pretend);
    }

    QVERIFY(consistent_copy::looksLikeSqlite(file));
    QVERIFY(!consistent_copy::readSqliteDatabase(file));

    const auto copied = consistent_copy::readFile(file, static_cast<quint64>(pretend.size()));
    QVERIFY2(copied.operator bool(), "a file that only looks like a database was not copied");
    QCOMPARE(*copied, pretend);
}

void ConsistentCopyTest::anOrdinaryFileIsReadAsItIs() {
    const QByteArray body = QByteArray("a plain file\nwith two lines\n");
    const QString file = path(QStringLiteral("plain-read.txt"));
    {
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(body);
    }

    const auto copied = consistent_copy::readFile(file, static_cast<quint64>(body.size()));
    QVERIFY(copied.operator bool());
    QCOMPARE(*copied, body);
}

void ConsistentCopyTest::aFileThatShrankWhileItWasBeingReadIsRefused() {
    // The one that matters. Stored short, the archive would hold the truncated
    // bytes with a hash that matches them, and nothing anywhere would ever say
    // the file had been cut off.
    const QString file = path(QStringLiteral("shrinking.log"));
    {
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QByteArray(400, 'x'));
    }

    const auto refused = consistent_copy::readFile(file, 4096);
    QVERIFY2(!refused, "a file that lost most of itself between the scan and the read was kept");
    const QString said = QString::fromStdString(refused.error().toString());
    QVERIFY2(said.contains(QStringLiteral("shrinking.log")), qPrintable(said));
    QVERIFY2(said.contains(QStringLiteral("4096")), qPrintable(said));
    QVERIFY2(said.contains(QStringLiteral("400")), qPrintable(said));
}

void ConsistentCopyTest::aFileThatGrewIsFine() {
    // A log or a database that gained a line between being counted and being
    // read is ordinary, and taking the longer version is right.
    const QByteArray body(4096, 'y');
    const QString file = path(QStringLiteral("growing.log"));
    {
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(body);
    }

    const auto copied = consistent_copy::readFile(file, 400);
    QVERIFY2(copied.operator bool(), "a file that gained a line was refused");
    QCOMPARE(copied->size(), body.size());
}

void ConsistentCopyTest::withNoExpectedSizeNothingIsChecked() {
    const QString file = path(QStringLiteral("unchecked.txt"));
    {
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write("short");
    }
    QVERIFY(consistent_copy::readFile(file, 0).operator bool());
}

void ConsistentCopyTest::whatCannotBeReadIsSaidRatherThanReturnedEmpty() {
    const auto missing = consistent_copy::readFile(path(QStringLiteral("no-such-file")), 0);
    QVERIFY2(!missing, "a file that is not there came back as an empty one");
    QCOMPARE(missing.error().code, format::ErrorCode::PermissionDenied);

    // A directory opens on some systems and reads as nothing on others; either
    // way it must not come back as a file with no bytes in it.
    const QString folder = path(QStringLiteral("a-folder"));
    QVERIFY(QDir().mkpath(folder));
    const auto directory = consistent_copy::readFile(folder, 16);
    QVERIFY2(!directory, "a folder was copied as if it were a file");
}

}  // namespace transmit::core

QTEST_GUILESS_MAIN(transmit::core::ConsistentCopyTest)
#include "ConsistentCopyTest.moc"
