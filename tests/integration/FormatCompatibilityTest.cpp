// An archive written by an older Transmit still opens.
//
// The archive format is at version 2. Version 1 differed in three places -
// the footer carried eight bytes of the manifest's hash rather than
// thirty-two, each block header twelve of its own rather than sixteen, and a
// part checksummed only its header - and every one of those is a number this
// build now reads differently.
//
// Somebody with a stick in a drawer does not know or care about any of that.
// They plug it in and expect their files. So the fixture beside this test is
// a real version 1 archive, written by the build before the change and
// committed exactly as it came out, and what is checked here is the whole
// path they would take: it opens, it says what it holds, every block passes
// its own integrity check, and each file comes back byte for byte.
//
// The fixture must never be regenerated. Rewriting it with a current build
// would make this test pass by testing nothing, which is the only way it can
// fail to do its job.

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "core/services/ImportService.h"
#include "format/Container.h"
#include "format/VolumeSplitter.h"
#include "platform/PlatformService.h"

using namespace transmit;

class FormatCompatibilityTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void aVersionOneArchiveStillOpens();
    void everyBlockOfAVersionOneArchiveStillChecksOut();
    void aVersionOneArchiveStillRestoresByteForByte();
    void whatThisBuildWritesSaysItIsVersionTwo();
    void aFooterFromTheFutureIsRefusedRatherThanGuessedAt();
    void aPartWhoseBytesChangedIsCaughtWithoutUnpackingIt();
    void aPartWithNoRecordedChecksumIsSkippedRatherThanFailed();

private:
    [[nodiscard]] static std::filesystem::path fixture() {
        return std::filesystem::path(TRANSMIT_FIXTURE_DIR) / "archive-v1.txa";
    }

    /// What the fixture was made from, so "it restored" can mean something
    /// stronger than "files appeared".
    [[nodiscard]] static QHash<QString, QByteArray> expected() {
        return {
            {QStringLiteral("notes.txt"),
             QByteArrayLiteral("The quick brown fox jumps over the lazy dog.\n")},
            {QStringLiteral("table.csv"), QByteArrayLiteral("alpha,beta,gamma\n1,2,3\n")},
            {QStringLiteral("deeper.txt"), QByteArrayLiteral("a file one level down\n")},
        };
    }

    QTemporaryDir workspace_;
};

void FormatCompatibilityTest::initTestCase() {
    QVERIFY(workspace_.isValid());
    QVERIFY2(std::filesystem::exists(fixture()),
             "the committed version 1 archive is missing - it must not be deleted, and it must "
             "never be regenerated with a current build");
}

void FormatCompatibilityTest::aVersionOneArchiveStillOpens() {
    auto reader = format::ArchiveReader::open(fixture());
    QVERIFY2(reader.operator bool(), "a version 1 archive could not be opened");

    // It really is the old format, or the rest of this file is checking that
    // the current build can read what the current build writes.
    QCOMPARE((*reader)->version(), static_cast<quint16>(1));

    auto manifest = (*reader)->manifest();
    QVERIFY2(manifest.operator bool(), "the manifest of a version 1 archive could not be read");

    int files = 0;
    for (const format::ManifestEntry& entry : (*manifest)->entries) {
        if (entry.type == format::EntryType::File) {
            ++files;
        }
    }
    QCOMPARE(files, expected().size());
}

void FormatCompatibilityTest::everyBlockOfAVersionOneArchiveStillChecksOut() {
    auto reader = format::ArchiveReader::open(fixture());
    QVERIFY(reader.operator bool());

    // The block headers of a version 1 archive carry twelve bytes of hash
    // where this build writes sixteen. Comparing all sixteen against a header
    // that only ever held twelve would fail every block in it - and would fail
    // as an integrity error, which is the most alarming way to be wrong.
    const auto status = (*reader)->verifyAllBlocks([](std::size_t, std::size_t) { return true; });
    QVERIFY2(status.operator bool(),
             status ? "" : qPrintable(QString::fromStdString(status.error().toString())));
}

void FormatCompatibilityTest::aVersionOneArchiveStillRestoresByteForByte() {
    auto platform = platform::PlatformService::create();
    core::ImportService importer(*platform);
    core::CancelToken token;

    const QString destination = workspace_.filePath(QStringLiteral("restored"));
    QVERIFY(QDir().mkpath(destination));

    core::ImportRequest request;
    request.archivePath = QString::fromStdString(fixture().string());
    request.destinationOverride = destination;
    request.conflictPolicy = core::ConflictPolicy::Overwrite;
    request.createRollback = false;
    request.durableWrites = false;
    request.keepJournal = false;

    const core::ImportReport report = importer.run(request, token);
    QVERIFY2(report.succeeded, qPrintable(report.errorMessage));

    const QHash<QString, QByteArray> wanted = expected();
    int checked = 0;
    QDirIterator walk(destination, QDir::Files, QDirIterator::Subdirectories);
    while (walk.hasNext()) {
        const QString path = walk.next();
        const auto found = wanted.constFind(QFileInfo(path).fileName());
        if (found == wanted.constEnd()) {
            continue;
        }
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
        QCOMPARE(file.readAll(), found.value());
        ++checked;
    }
    QCOMPARE(checked, wanted.size());
}

void FormatCompatibilityTest::whatThisBuildWritesSaysItIsVersionTwo() {
    const std::filesystem::path made =
        std::filesystem::path(workspace_.filePath(QStringLiteral("made.txa")).toStdString());

    format::ArchiveOptions options;
    options.preset = format::CompressionPreset::Fast;
    auto writer = format::ArchiveWriter::create(made, options);
    QVERIFY(writer.operator bool());
    QVERIFY((*writer)->writeBlock(format::asBytes(std::string(512, 'q'))).operator bool());
    format::Manifest manifest;
    manifest.source.os = format::OsFamily::Linux;
    QVERIFY((*writer)->finish(manifest));
    writer->reset();

    auto reader = format::ArchiveReader::open(made);
    QVERIFY(reader.operator bool());
    QCOMPARE((*reader)->version(), format::ArchiveHeader::kVersion);
    QVERIFY((*reader)->manifest().operator bool());

    // The point of the version being 2 rather than "whatever the footer looks
    // like": the whole manifest hash is committed to, not the first eight
    // bytes of it.
    const auto status = (*reader)->verifyAllBlocks([](std::size_t, std::size_t) { return true; });
    QVERIFY(status.operator bool());
}

void FormatCompatibilityTest::aFooterFromTheFutureIsRefusedRatherThanGuessedAt() {
    const QString made = workspace_.filePath(QStringLiteral("future.txa"));
    QFile::remove(made);
    QVERIFY(QFile::copy(QString::fromStdString(fixture().string()), made));
    QVERIFY(QFile::setPermissions(made, QFileDevice::ReadOwner | QFileDevice::WriteOwner));

    // The archive header says which footer is at the end, so a version this
    // build has never heard of has to stop at the header rather than read
    // eighty bytes as though it understood them.
    QFile file(made);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QByteArray bytes = file.readAll();
    const qsizetype header = bytes.indexOf(QByteArrayLiteral("TXA1"));
    QVERIFY2(header >= 0, "the fixture has no archive header");
    bytes[header + 4] = static_cast<char>(0xFE);
    bytes[header + 5] = static_cast<char>(0xFF);
    file.seek(0);
    file.write(bytes);
    file.close();

    auto reader = format::ArchiveReader::open(std::filesystem::path(made.toStdString()));
    QVERIFY2(!reader.operator bool(), "read an archive written by a version it does not know");
}

// The reason the part checksum was worth a version.
//
// Before it, a part vouched for its own forty-byte header and for nothing
// after it. Damage in the payload was still caught - every block is hashed -
// but only by decompressing every block, which on the drive somebody is
// worried about is the slow way to find out. This finds it in one pass.
void FormatCompatibilityTest::aPartWhoseBytesChangedIsCaughtWithoutUnpackingIt() {
    const QString made = workspace_.filePath(QStringLiteral("rotted.txa"));
    const std::filesystem::path path(made.toStdString());

    format::ArchiveOptions options;
    options.preset = format::CompressionPreset::Fast;
    auto writer = format::ArchiveWriter::create(path, options);
    QVERIFY(writer.operator bool());
    QVERIFY((*writer)->writeBlock(format::asBytes(std::string(8192, 'm'))).operator bool());
    format::Manifest manifest;
    manifest.source.os = format::OsFamily::Linux;
    QVERIFY((*writer)->finish(manifest));
    writer->reset();

    {
        auto source = format::VolumeSource::open(path);
        QVERIFY(source.operator bool());
        std::size_t skipped = 0;
        QVERIFY2((*source)->verifyPayloadChecksums(nullptr, &skipped).operator bool(),
                 "an archive this build just wrote failed its own part checksum");
        QCOMPARE(skipped, std::size_t{0});
    }

    // One byte, in the middle of the payload, where no header lives.
    QFile file(made);
    QVERIFY(file.open(QIODevice::ReadWrite));
    const qint64 middle = file.size() / 2;
    QVERIFY(file.seek(middle));
    char byte = 0;
    QCOMPARE(file.read(&byte, 1), 1);
    QVERIFY(file.seek(middle));
    byte = static_cast<char>(byte ^ 0x01);
    QCOMPARE(file.write(&byte, 1), 1);
    file.close();

    auto source = format::VolumeSource::open(path);
    QVERIFY2(source.operator bool(), "a flipped payload byte should not stop the part opening");

    const auto status = (*source)->verifyPayloadChecksums(nullptr, nullptr);
    QVERIFY2(!status.operator bool(), "a flipped payload byte went unnoticed");
    QCOMPARE(status.error().code, format::ErrorCode::IntegrityMismatch);
}

// An archive from before the checksum existed has none, and saying "damaged"
// about it would be worse than saying nothing: it is not damaged, it is old.
void FormatCompatibilityTest::aPartWithNoRecordedChecksumIsSkippedRatherThanFailed() {
    auto source = format::VolumeSource::open(fixture());
    QVERIFY(source.operator bool());

    std::size_t skipped = 0;
    const auto status = (*source)->verifyPayloadChecksums(nullptr, &skipped);
    QVERIFY2(status.operator bool(), "a version 1 part was reported as damaged for being old");
    QCOMPARE(skipped, std::size_t{1});
}

QTEST_MAIN(FormatCompatibilityTest)
#include "FormatCompatibilityTest.moc"
