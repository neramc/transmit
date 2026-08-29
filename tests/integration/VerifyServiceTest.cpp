// Reading the archive back off the drive, and meaning it.
//
// "Verified" is a claim about a piece of hardware, not about a data structure.
// The archive's own hashes prove it is self-consistent; they say nothing about
// whether the stick kept what it acknowledged. These tests hold the read-back
// to the difference: a fresh reader, the cache dropped first where the system
// allows, every read retried with the attempts counted, and a failure named
// against the file it ruined rather than reported as one line about a block.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "core/services/ExportService.h"
#include "core/services/ProfileService.h"
#include "core/services/RepairService.h"
#include "core/services/VerifyService.h"
#include "core/utils/Conversions.h"
#include "format/Container.h"
#include "format/IoHooks.h"
#include "format/hash/Blake2b.h"
#include "platform/PlatformService.h"

using namespace transmit;

class VerifyServiceTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void anArchiveThatArrivedIntactVerifies();
    void aSplitArchiveIsVerifiedByOpeningOneOfItsParts();
    void aFlippedBitIsNamedAgainstTheFileItRuined();
    void aReadThatOnlyWorksOnTheSecondTryIsCountedNotHidden();
    void aDriveThatWillNotGiveAFileBackSaysWhichFile();
    void aPartThatDoesNotMatchItsChecksumFailsTheRun();
    void theReportSaysWhetherItManagedToReadPastTheCache();
    void verificationFailingFailsTheCaptureThatAskedForIt();
    void theCaptureCanBeToldNotToVerify();

    void aDamagedFileIsRecoveredFromTheMachineItCameFrom();
    void aRepairCannotSubstituteContentTheArchiveNeverClaimed();
    void aFileThatHasChangedSinceTheCaptureIsNotUsedToRepairIt();
    void repairingASoundArchiveDoesNothingAndSaysSo();
    void repairingTwiceFindsNothingLeftToDo();

private:
    /// Captures the fixture home and returns the archive path.
    [[nodiscard]] QString capture(const QString& name, bool verifyAfterWriting = false);

    [[nodiscard]] QString home() const { return workspace_.filePath("home"); }
    [[nodiscard]] QString archivePath(const QString& name) const {
        return workspace_.filePath("archives/" + name);
    }

    /// Flips one bit somewhere in the middle of a file.
    static void damage(const QString& path, qint64 offset);

    /// The documents the captures in here are made of. Called again by the
    /// test that deliberately rewrites them, so the tests after it are not
    /// quietly capturing something else.
    void writeFixture();

    QTemporaryDir workspace_;
    std::unique_ptr<platform::PlatformService> platform_;
    QString originalHome_;
    bool usable_ = false;
};

void VerifyServiceTest::initTestCase() {
    QVERIFY(workspace_.isValid());
    QVERIFY(QDir().mkpath(home() + QStringLiteral("/Documents")));
    QVERIFY(QDir().mkpath(workspace_.filePath("archives")));

    writeFixture();

    originalHome_ = qEnvironmentVariable("HOME");
    qputenv("HOME", home().toUtf8());
    platform_ = platform::PlatformService::create();

    // Windows resolves its known folders through the shell rather than from
    // HOME, so the capture below would read the account's real documents.
    const auto documents = platform_->knownFolders().base(format::PathTokenId::Documents);
    usable_ = documents && QString::fromStdString(*documents).startsWith(home());
    if (!usable_) {
        QSKIP("this platform does not resolve its known folders from HOME");
    }
}

void VerifyServiceTest::writeFixture() {
    const auto write = [](const QString& path, const QByteArray& content) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
        file.write(content);
    };
    write(home() + QStringLiteral("/Documents/notes.txt"), "a few lines worth keeping\n");
    write(home() + QStringLiteral("/Documents/report.txt"), QByteArray(9000, 'r'));

    // Incompressible, so damaging a byte of the payload really does change the
    // bytes a file is made of rather than a run length that happens to repeat.
    QByteArray noise(120000, '\0');
    quint32 state = 0x51ed270bu;
    for (char& byte : noise) {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<char>((state >> 24) & 0xFFu);
    }
    write(home() + QStringLiteral("/Documents/noise.bin"), noise);
}

void VerifyServiceTest::cleanupTestCase() {
    format::setIoHooks(nullptr);
    if (!originalHome_.isEmpty()) {
        qputenv("HOME", originalHome_.toUtf8());
    }
}

QString VerifyServiceTest::capture(const QString& name, bool verifyAfterWriting) {
    core::ExportService exporter(*platform_);
    core::CancelToken token;

    core::ExportRequest request;
    request.destinationPath = archivePath(name);
    request.selection = core::ProfileService::profileById(QStringLiteral("documents")).selection;
    request.packaging.preset = format::CompressionPreset::Fast;
    request.packaging.verifyAfterWriting = verifyAfterWriting;

    const core::ExportReport report = exporter.run(request, token, {});
    if (!report.succeeded) {
        return {};
    }
    return request.destinationPath;
}

void VerifyServiceTest::damage(const QString& path, qint64 offset) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.seek(offset));
    char byte = 0;
    QCOMPARE(file.read(&byte, 1), 1);
    QVERIFY(file.seek(offset));
    byte = static_cast<char>(byte ^ 0x01);
    QCOMPARE(file.write(&byte, 1), 1);
}

void VerifyServiceTest::anArchiveThatArrivedIntactVerifies() {
    const QString archive = capture(QStringLiteral("good.txa"));
    QVERIFY(!archive.isEmpty());

    core::CancelToken token;
    core::VerifyRequest request;
    request.archivePath = archive;

    const core::VerifyService verifier(*platform_);
    const core::VerifyReport report = verifier.run(request, token, {});

    QVERIFY2(report.succeeded, qPrintable(report.errorMessage));
    QVERIFY(report.everythingMatched());
    QCOMPARE(report.filesFailed, 0ULL);
    QVERIFY2(report.filesChecked >= 3, qPrintable(QString::number(report.filesChecked)));
    QVERIFY(report.failures.isEmpty());
    QCOMPARE(report.retriedReads, 0ULL);
}

// A split archive has no file at the base name at all - the parts are
// name.txa.001 and so on - so a verification that opened the name it was told
// to write would fail with "no such file" after a capture that went perfectly.
void VerifyServiceTest::aSplitArchiveIsVerifiedByOpeningOneOfItsParts() {
    core::ExportService exporter(*platform_);
    core::CancelToken token;

    core::ExportRequest request;
    request.destinationPath = archivePath(QStringLiteral("split.txa"));
    request.selection = core::ProfileService::profileById(QStringLiteral("documents")).selection;
    request.packaging.preset = format::CompressionPreset::Fast;
    request.packaging.partSize = 64 * 1024;
    request.packaging.verifyAfterWriting = true;

    const core::ExportReport report = exporter.run(request, token, {});
    QVERIFY2(report.succeeded, qPrintable(report.errorMessage));
    QVERIFY2(report.archiveParts.size() > 1,
             "the part size was too large for this to have split at all");
    QVERIFY2(!QFileInfo::exists(request.destinationPath),
             "a split archive should not leave a file at the base name");

    QVERIFY(report.verificationRan);
    QVERIFY2(report.verified, "a split archive was written and could not be read back");
    QVERIFY(report.verifiedFiles > 0);
}

// The point of checking each file rather than each block: the person reading
// this is about to decide whether to wipe the machine it came from, and "block
// 3 is bad" does not tell them what they are about to lose.
void VerifyServiceTest::aFlippedBitIsNamedAgainstTheFileItRuined() {
    const QString archive = capture(QStringLiteral("damaged.txa"));
    QVERIFY(!archive.isEmpty());

    damage(archive, QFileInfo(archive).size() / 2);

    core::CancelToken token;
    core::VerifyRequest request;
    request.archivePath = archive;
    // The sidecar would catch this first and say nothing about which file it
    // was, which is the case the per-file check exists to improve on.
    request.useSidecar = false;

    const core::VerifyService verifier(*platform_);
    const core::VerifyReport report = verifier.run(request, token, {});

    QVERIFY(!report.everythingMatched());
    // Not only everythingMatched(): a run that read the whole archive and
    // found two files wrong did not succeed, however smoothly it finished, and
    // a caller looking at `succeeded` alone has to be told the truth.
    QVERIFY2(!report.succeeded, "files failed and the run still called itself a success");
    QVERIFY(!report.errorMessage.isEmpty());
    QVERIFY2(report.filesFailed > 0, "a bit was flipped and every file still matched");
    QVERIFY(!report.failures.isEmpty());
    QVERIFY2(!report.failures.constFirst().path.isEmpty(),
             "a failure was reported without saying which file it was");
}

// A stick that answers on the second attempt is working and dying. Forgiving
// it silently is how somebody finds out later.
void VerifyServiceTest::aReadThatOnlyWorksOnTheSecondTryIsCountedNotHidden() {
    const QString archive = capture(QStringLiteral("flaky.txa"));
    QVERIFY(!archive.isEmpty());

    // Aimed at the block payloads rather than at the first two reads of the
    // file: the first two are the volume header and the footer, and stalling
    // on those tests the opening path rather than the reading-back one.
    int refusals = 2;
    format::IoHooks hooks;
    hooks.beforeRead = [&refusals](const std::filesystem::path& path, std::uint64_t,
                                   std::size_t size) -> std::optional<format::Error> {
        if (path.extension() != ".txa" || size < 1024 || refusals <= 0) {
            return std::nullopt;
        }
        --refusals;
        format::Error error = format::makeError(format::ErrorCode::IoError, "the drive stalled");
        error.systemCode = EIO;
        return error;
    };
    const format::ScopedIoHooks installed(std::move(hooks));

    core::CancelToken token;
    core::VerifyRequest request;
    request.archivePath = archive;
    request.useSidecar = false;

    const core::VerifyService verifier(*platform_);
    const core::VerifyReport report = verifier.run(request, token, {});

    QVERIFY2(report.succeeded, qPrintable(report.errorMessage));
    QCOMPARE(report.filesFailed, 0ULL);
    QVERIFY2(report.retriedReads > 0, "a read was retried and the report did not say so");
}

void VerifyServiceTest::aDriveThatWillNotGiveAFileBackSaysWhichFile() {
    const QString archive = capture(QStringLiteral("dead.txa"));
    QVERIFY(!archive.isEmpty());

    // Every block read fails, but the manifest is already loaded by then, so
    // the run gets far enough to say which files were in the blocks it lost.
    bool manifestLoaded = false;
    format::IoHooks hooks;
    hooks.beforeRead = [&manifestLoaded](const std::filesystem::path& path, std::uint64_t offset,
                                         std::size_t) -> std::optional<format::Error> {
        if (path.extension() != ".txa") {
            return std::nullopt;
        }
        // Let everything through until the archive has been opened and its
        // manifest read; the manifest lives at the end of the file.
        if (!manifestLoaded) {
            if (offset > 0) {
                manifestLoaded = true;
            }
            return std::nullopt;
        }
        format::Error error = format::makeError(format::ErrorCode::IoError, "the drive is gone");
        error.systemCode = EIO;
        return error;
    };
    const format::ScopedIoHooks installed(std::move(hooks));

    core::CancelToken token;
    core::VerifyRequest request;
    request.archivePath = archive;
    request.useSidecar = false;

    const core::VerifyService verifier(*platform_);
    const core::VerifyReport report = verifier.run(request, token, {});

    QVERIFY(!report.everythingMatched());
    QVERIFY(!report.succeeded);
    for (const core::VerifyFileResult& failure : report.failures) {
        QVERIFY(!failure.path.isEmpty());
        QVERIFY(!failure.detail.isEmpty());
    }
}

// The archive's own hashes cannot see this: a part that was truncated after
// the footer was written still holds a consistent prefix. The checksum written
// beside it can.
void VerifyServiceTest::aPartThatDoesNotMatchItsChecksumFailsTheRun() {
    const QString archive = capture(QStringLiteral("sidecar.txa"));
    QVERIFY(!archive.isEmpty());
    QVERIFY2(QFileInfo::exists(archive + QStringLiteral(".md5")),
             "the capture did not write a checksum file");

    damage(archive, 200);

    core::CancelToken token;
    core::VerifyRequest request;
    request.archivePath = archive;

    const core::VerifyService verifier(*platform_);
    const core::VerifyReport report = verifier.run(request, token, {});

    QVERIFY(!report.succeeded);
    bool complained = false;
    for (const core::VerifyPartResult& part : report.parts) {
        complained = complained || !part.md5Matched;
    }
    QVERIFY2(complained, "the part no longer matches its checksum and nothing said so");
}

void VerifyServiceTest::theReportSaysWhetherItManagedToReadPastTheCache() {
    const QString archive = capture(QStringLiteral("cold.txa"));
    QVERIFY(!archive.isEmpty());

    core::CancelToken token;
    core::VerifyRequest request;
    request.archivePath = archive;

    const core::VerifyService verifier(*platform_);
    const core::VerifyReport report = verifier.run(request, token, {});
    QVERIFY(report.succeeded);

#if defined(Q_OS_LINUX)
    // Linux can evict, so a false here would mean the read-back was served
    // from memory while claiming to have checked the drive.
    QVERIFY2(report.cacheDropped, "Linux can drop the page cache and this run did not");
#else
    // Elsewhere it cannot, and the flag has to say so rather than default to
    // the comfortable answer.
    QVERIFY(!report.cacheDropped);
#endif

    request.dropCache = false;
    const core::VerifyReport without = verifier.run(request, token, {});
    QVERIFY(without.succeeded);
    QVERIFY(!without.cacheDropped);
}

// verifyAfterWriting was settable from the command line and the selection file
// for a release and did nothing at all. This is the test that says it does.
void VerifyServiceTest::verificationFailingFailsTheCaptureThatAskedForIt() {
    // Reads of the finished archive fail, so the verification cannot confirm
    // anything - and a capture that cannot be confirmed is not a success.
    format::IoHooks hooks;
    hooks.beforeRead = [](const std::filesystem::path& path, std::uint64_t,
                          std::size_t) -> std::optional<format::Error> {
        if (path.extension() != ".txa") {
            return std::nullopt;
        }
        format::Error error = format::makeError(format::ErrorCode::IoError, "the stick was pulled");
        error.systemCode = EIO;
        return error;
    };
    const format::ScopedIoHooks installed(std::move(hooks));

    core::ExportService exporter(*platform_);
    core::CancelToken token;

    core::ExportRequest request;
    request.destinationPath = archivePath(QStringLiteral("unverifiable.txa"));
    request.selection = core::ProfileService::profileById(QStringLiteral("documents")).selection;
    request.packaging.preset = format::CompressionPreset::Fast;
    request.packaging.verifyAfterWriting = true;

    const core::ExportReport report = exporter.run(request, token, {});
    QVERIFY2(!report.succeeded,
             "the archive could not be read back and the capture said it worked");
    QVERIFY(report.verificationRan);
    QVERIFY(!report.verified);
    QVERIFY(!report.errorMessage.isEmpty());
}

void VerifyServiceTest::theCaptureCanBeToldNotToVerify() {
    format::IoHooks hooks;
    hooks.beforeRead = [](const std::filesystem::path& path, std::uint64_t,
                          std::size_t) -> std::optional<format::Error> {
        if (path.extension() != ".txa") {
            return std::nullopt;
        }
        format::Error error = format::makeError(format::ErrorCode::IoError, "the stick was pulled");
        error.systemCode = EIO;
        return error;
    };
    const format::ScopedIoHooks installed(std::move(hooks));

    core::ExportService exporter(*platform_);
    core::CancelToken token;

    core::ExportRequest request;
    request.destinationPath = archivePath(QStringLiteral("unchecked.txa"));
    request.selection = core::ProfileService::profileById(QStringLiteral("documents")).selection;
    request.packaging.preset = format::CompressionPreset::Fast;
    request.packaging.verifyAfterWriting = false;

    const core::ExportReport report = exporter.run(request, token, {});
    QVERIFY2(report.succeeded, qPrintable(report.errorMessage));
    QVERIFY(!report.verificationRan);
}

// ------------------------------------------------------------- repairing

// The damaged archive is never touched: its footer and part lengths are
// computed over the whole set, so writing a corrected file back into it would
// invalidate the thing being fixed. The recovered files go beside it instead,
// and every reader picks them up on its own.
void VerifyServiceTest::aDamagedFileIsRecoveredFromTheMachineItCameFrom() {
    const QString archive = capture(QStringLiteral("repairable.txa"));
    QVERIFY(!archive.isEmpty());

    const QByteArray before = [&archive] {
        QFile file(archive);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }();
    QVERIFY(!before.isEmpty());

    damage(archive, QFileInfo(archive).size() / 2);

    core::CancelToken token;
    core::RepairRequest request;
    request.archivePath = archive;

    const core::RepairService repairer(*platform_);
    const core::RepairReport report = repairer.run(request, token, {});

    QVERIFY2(report.succeeded, qPrintable(report.errorMessage));
    QVERIFY2(report.filesNeedingRepair > 0, "nothing was found to need repairing");
    QCOMPARE(report.filesRepaired, report.filesNeedingRepair);
    QVERIFY(report.failures.isEmpty());
    QVERIFY(QFileInfo::exists(report.repairPath));

    // The original, byte for byte as the damage left it.
    QFile after(archive);
    QVERIFY(after.open(QIODevice::ReadOnly));
    const QByteArray now = after.readAll();
    QCOMPARE(now.size(), before.size());
    QVERIFY2(now != before, "the fixture did not actually damage anything");

    // And the files now read correctly, without anybody asking for the repair.
    auto reader = format::ArchiveReader::open(format::toFsPath(core::toUtf8(archive)));
    QVERIFY(reader);
    const auto manifest = (*reader)->manifest();
    QVERIFY(manifest);
    QVERIFY2((*reader)->hasRepair(), "the repair was written and the reader did not pick it up");

    int read = 0;
    for (const format::ManifestEntry& entry : (*manifest)->entries) {
        if (entry.hasContent()) {
            QVERIFY2((*reader)->readEntry(entry),
                     qPrintable(QString::fromStdString(entry.path.toDisplayString())));
            ++read;
        }
    }
    QVERIFY(read > 0);
}

// A repair may only supply bytes that hash to what the archive already
// recorded for that path. Without that rule, dropping a file called
// `name.txa.repair` next to somebody's archive would change what restoring it
// puts on their machine.
void VerifyServiceTest::aRepairCannotSubstituteContentTheArchiveNeverClaimed() {
    const QString archive = capture(QStringLiteral("substitute.txa"));
    QVERIFY(!archive.isEmpty());

    // A perfectly valid archive holding the same paths with different
    // contents - which is what an attacker, or a muddle, produces.
    const QString impostorHome = workspace_.filePath(QStringLiteral("impostor"));
    QVERIFY(QDir().mkpath(impostorHome + QStringLiteral("/Documents")));
    const QString original = qEnvironmentVariable("HOME");
    qputenv("HOME", impostorHome.toUtf8());
    {
        auto impostorPlatform = platform::PlatformService::create();
        QFile file(impostorHome + QStringLiteral("/Documents/notes.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not the notes that were captured\n");
        file.close();

        core::ExportService exporter(*impostorPlatform);
        core::CancelToken token;
        core::ExportRequest request;
        request.destinationPath = archive + QStringLiteral(".repair");
        request.selection =
            core::ProfileService::profileById(QStringLiteral("documents")).selection;
        request.packaging.preset = format::CompressionPreset::Fast;
        request.packaging.verifyAfterWriting = false;
        const core::ExportReport made = exporter.run(request, token, {});
        QVERIFY2(made.succeeded, qPrintable(made.errorMessage));
    }
    qputenv("HOME", original.toUtf8());

    auto reader = format::ArchiveReader::open(format::toFsPath(core::toUtf8(archive)));
    QVERIFY(reader);
    const auto manifest = (*reader)->manifest();
    QVERIFY(manifest);

    // Nothing in it hashes to what this archive recorded, so nothing is taken
    // from it. The archive reads exactly as it did before.
    QVERIFY2(!(*reader)->hasRepair(),
             "a repair holding different content was accepted as a replacement");

    for (const format::ManifestEntry& entry : (*manifest)->entries) {
        if (!entry.hasContent()) {
            continue;
        }
        const auto content = (*reader)->readEntry(entry);
        QVERIFY(content);
        QCOMPARE(format::Blake2b::hash256(format::ByteView(*content)), entry.contentHash);
    }
}

// Putting the new version in would produce an archive that passes every check
// and does not hold what it says it holds.
void VerifyServiceTest::aFileThatHasChangedSinceTheCaptureIsNotUsedToRepairIt() {
    const QString archive = capture(QStringLiteral("moved-on.txa"));
    QVERIFY(!archive.isEmpty());

    damage(archive, QFileInfo(archive).size() / 2);

    // Everything the capture read is now something else.
    for (const QString& name :
         {QStringLiteral("notes.txt"), QStringLiteral("report.txt"), QStringLiteral("noise.bin")}) {
        QFile file(home() + QStringLiteral("/Documents/") + name);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("rewritten since the capture\n");
    }

    core::CancelToken token;
    core::RepairRequest request;
    request.archivePath = archive;

    const core::RepairService repairer(*platform_);
    const core::RepairReport report = repairer.run(request, token, {});

    QVERIFY2(!report.succeeded, "a changed file was accepted as a repair for the old one");
    QCOMPARE(report.filesRepaired, 0ULL);
    QVERIFY(!report.failures.isEmpty());
    for (const core::RepairFailure& failure : report.failures) {
        QCOMPARE(failure.obstacle, core::RepairObstacle::SourceChanged);
    }
    QVERIFY2(!QFileInfo::exists(archive + QStringLiteral(".repair")),
             "a repair archive was left behind holding nothing usable");

    // Put the fixture back: the tests after this one capture the same folder,
    // and leaving it rewritten would have them measuring something else.
    writeFixture();
}

// A file an existing repair already supplies is reported by verification
// because the archive is damaged, not because the file is lost. Recovering it
// again would recover something that is already there and call it work.
void VerifyServiceTest::repairingTwiceFindsNothingLeftToDo() {
    const QString archive = capture(QStringLiteral("twice.txa"));
    QVERIFY(!archive.isEmpty());

    damage(archive, QFileInfo(archive).size() / 2);

    core::CancelToken token;
    core::RepairRequest request;
    request.archivePath = archive;

    const core::RepairService repairer(*platform_);
    const core::RepairReport first = repairer.run(request, token, {});
    QVERIFY2(first.succeeded, qPrintable(first.errorMessage));
    QVERIFY(first.filesRepaired > 0);

    const core::RepairReport again = repairer.run(request, token, {});
    QVERIFY2(again.succeeded, qPrintable(again.errorMessage));
    QCOMPARE(again.filesNeedingRepair, 0ULL);
    QCOMPARE(again.filesRepaired, 0ULL);
}

void VerifyServiceTest::repairingASoundArchiveDoesNothingAndSaysSo() {
    const QString archive = capture(QStringLiteral("sound.txa"));
    QVERIFY(!archive.isEmpty());

    core::CancelToken token;
    core::RepairRequest request;
    request.archivePath = archive;

    const core::RepairService repairer(*platform_);
    const core::RepairReport report = repairer.run(request, token, {});

    QVERIFY2(report.succeeded, qPrintable(report.errorMessage));
    QCOMPARE(report.filesNeedingRepair, 0ULL);
    QCOMPARE(report.filesRepaired, 0ULL);
    QVERIFY(report.repairPath.isEmpty());
    QVERIFY2(!QFileInfo::exists(archive + QStringLiteral(".repair")),
             "a sound archive was given a repair file it does not need");
}

QTEST_MAIN(VerifyServiceTest)
#include "VerifyServiceTest.moc"
