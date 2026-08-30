// Offering to finish a capture the drive interrupted.
//
// The command line can already carry one on. The window is where most people
// will meet the situation, and there the offer has to appear by itself: nobody
// who has just watched a capture die is going to go looking for a flag. So the
// controller reads the record beside each archive in the chosen folder and
// says what it found - and, just as importantly, says nothing when there is
// nothing to say.

#include <QDir>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "app/ExportController.h"
#include "app/ImportController.h"
#include "core/services/ExportService.h"
#include "core/services/ProfileService.h"
#include "core/services/RollbackWriter.h"
#include "core/utils/Conversions.h"
#include "format/Container.h"
#include "format/IoHooks.h"
#include "format/Manifest.h"
#include "format/RestoreJournal.h"
#include "format/TransferJournal.h"
#include "platform/PlatformService.h"

using namespace transmit;
using transmit::app::ExportController;
using transmit::app::ImportController;

class CarryOnTest : public QObject {
    Q_OBJECT

private slots:
    void init();

    void anUnfinishedCaptureIsOffered();
    void aFinishedCaptureIsNotOffered();
    void aFolderWithNothingInItOffersNothing();
    void aJournalWithNoBlocksYetIsNotOffered();
    void theNewestUnfinishedCaptureIsTheOneOffered();
    void theOfferCanBeTakenUpAndPutDownAgain();
    void finishingACaptureSaysSoToTheEjectOffer();

    void anUnfinishedRestoreIsOffered();
    void aFinishedRestoreIsNotOffered();
    void aRestoreRecordDescribingNothingIsNotOffered();
    void aRestoreIntoADifferentFolderIsNotOffered();
    void theRestoreOfferCanBeTakenUpAndPutDownAgain();
    void theRestoreOfferOnlySaysSomethingWhenTheAnswerChanges();
    void takingTheRestoreOfferUpActuallyCarriesTheRestoreOn();

private:
    /// Writes an archive and the record beside it, as an interrupted run
    /// leaves them: no footer, and a journal saying what reached the drive.
    void leaveAnUnfinishedCapture(const QString& name, bool withABlock = true);

    /// Writes a whole archive and leaves its record beside it, marked
    /// finished. A capture normally deletes that record, but it can outlive
    /// the deletion - a drive that was pulled out between the two - and what
    /// it says has to be believed either way.
    void leaveAFinishedCapture(const QString& name);

    /// Writes a whole archive, then a restore record beside a destination
    /// saying a run into it stopped part way. `itemsDone` of nought is a
    /// record that describes nothing, which is a different answer.
    void leaveAnUnfinishedRestore(const QString& archiveName, const QString& destination,
                                  int itemsDone = 3, bool finished = false);

    [[nodiscard]] QString folder() const { return workspace_.path(); }
    [[nodiscard]] QString pathTo(const QString& name) const {
        return QDir(folder()).filePath(name);
    }

    QTemporaryDir workspace_;
};

void CarryOnTest::init() {
    // A fresh folder for each case: the controller reports the newest record
    // it finds, so one left behind by an earlier case would be the answer.
    QVERIFY(workspace_.isValid());
    const QDir directory(folder());
    for (const QString& name : directory.entryList(QDir::Files)) {
        QVERIFY(QFile::remove(directory.filePath(name)));
    }
}

void CarryOnTest::leaveAnUnfinishedCapture(const QString& name, bool withABlock) {
    const std::filesystem::path archive = format::toFsPath(core::toUtf8(pathTo(name)));

    format::ArchiveOptions options;
    options.preset = format::CompressionPreset::Fast;
    options.solidBlockSize = 4096;

    auto writer = format::ArchiveWriter::create(archive, options);
    QVERIFY(writer.operator bool());

    format::JournalFingerprint print;
    print.archiveUuid = (*writer)->uuid();
    print.destination = core::toUtf8(pathTo(name));
    print.hostName = "workshop";

    auto journal = format::TransferJournal::begin(archive, print);
    QVERIFY(journal.operator bool());

    if (withABlock) {
        const std::string text(2048, 'x');
        const auto bytes = format::asBytes(text);
        const auto blockId = (*writer)->writeBlock(bytes);
        QVERIFY(blockId.operator bool());

        const format::BlockRecord& record = (*writer)->blocks().back();
        format::JournalBlock block;
        block.blockId = record.blockId;
        block.streamOffset = record.streamOffset;
        block.logicalEnd = (*writer)->logicalLength();
        block.rawSize = record.rawSize;
        block.storedSize = record.storedSize;
        block.codec = record.codec;
        block.encrypted = record.encrypted;
        QVERIFY((*journal)->recordBlock(block));
    }

    QVERIFY((*journal)->close());
    // The writer goes without finish(), which is what an interrupted run
    // leaves: no manifest, no footer, nothing that can be opened.
}

void CarryOnTest::leaveAFinishedCapture(const QString& name) {
    const std::filesystem::path archive = format::toFsPath(core::toUtf8(pathTo(name)));

    format::ArchiveOptions options;
    options.preset = format::CompressionPreset::Fast;

    auto writer = format::ArchiveWriter::create(archive, options);
    QVERIFY(writer.operator bool());

    format::JournalFingerprint print;
    print.archiveUuid = (*writer)->uuid();
    print.destination = core::toUtf8(pathTo(name));
    auto journal = format::TransferJournal::begin(archive, print);
    QVERIFY(journal.operator bool());

    // A block, recorded, so that "finished" is the only thing standing
    // between this and an offer to carry on. Without it the record would
    // describe nothing and be turned down for that reason instead.
    const std::string text(2048, 'y');
    QVERIFY((*writer)->writeBlock(format::asBytes(text)).operator bool());
    const format::BlockRecord& record = (*writer)->blocks().back();
    format::JournalBlock block;
    block.blockId = record.blockId;
    block.streamOffset = record.streamOffset;
    block.logicalEnd = (*writer)->logicalLength();
    block.rawSize = record.rawSize;
    block.storedSize = record.storedSize;
    block.codec = record.codec;
    block.encrypted = record.encrypted;
    QVERIFY((*journal)->recordBlock(block));

    format::Manifest manifest;
    manifest.source.os = format::OsFamily::Linux;
    QVERIFY((*writer)->finish(manifest));

    QVERIFY((*journal)->recordComplete());
    QVERIFY((*journal)->close());
}

void CarryOnTest::leaveAnUnfinishedRestore(const QString& archiveName, const QString& destination,
                                           int itemsDone, bool finished) {
    const std::filesystem::path archive = format::toFsPath(core::toUtf8(pathTo(archiveName)));

    format::ArchiveOptions options;
    options.preset = format::CompressionPreset::Fast;

    auto writer = format::ArchiveWriter::create(archive, options);
    QVERIFY(writer.operator bool());
    QVERIFY((*writer)->writeBlock(format::asBytes(std::string(1024, 'z'))).operator bool());
    format::Manifest manifest;
    manifest.source.os = format::OsFamily::Linux;
    QVERIFY((*writer)->finish(manifest));
    const format::ArchiveUuid uuid = (*writer)->uuid();

    const QString state =
        QDir(destination).filePath(QString::fromLatin1(core::RollbackWriter::kDirectoryName));
    QVERIFY(QDir().mkpath(state));

    format::RestoreFingerprint print;
    print.archiveUuid = uuid;
    print.destination = core::toUtf8(destination);

    auto journal = format::RestoreJournal::begin(
        format::RestoreJournal::pathFor(format::toFsPath(core::toUtf8(state)), uuid), print);
    QVERIFY(journal.operator bool());

    for (int i = 0; i < itemsDone; ++i) {
        format::RestorePlacement placement;
        placement.source = "{DOCUMENTS}/file-" + std::to_string(i);
        placement.target =
            core::toUtf8(QDir(destination).filePath(QStringLiteral("file-%1").arg(i)));
        placement.outcome = format::RestoreOutcome::Written;
        QVERIFY((*journal)->recordPlacement(placement));
    }
    if (finished) {
        QVERIFY((*journal)->recordComplete());
    }
    QVERIFY((*journal)->close());
}

void CarryOnTest::anUnfinishedRestoreIsOffered() {
    const QString destination = QDir(folder()).filePath(QStringLiteral("into"));
    QVERIFY(QDir().mkpath(destination));
    leaveAnUnfinishedRestore(QStringLiteral("stopped-restore.txa"), destination);

    ImportController controller;
    QSignalSpy changed(&controller, &ImportController::carryOnChanged);

    controller.inspect(pathTo(QStringLiteral("stopped-restore.txa")));
    QTRY_VERIFY(!controller.isInspecting());
    controller.lookForInterruptedRestore(destination);

    QVERIFY2(controller.canCarryOn(), "an unfinished restore went unnoticed");
    QCOMPARE(changed.count(), 1);
    QVERIFY2(controller.carryOnText().contains(QStringLiteral("3")),
             qPrintable(controller.carryOnText()));
    QVERIFY2(!controller.isCarryingOn(), "it carried on without being asked to");
}

void CarryOnTest::aFinishedRestoreIsNotOffered() {
    const QString destination = QDir(folder()).filePath(QStringLiteral("into"));
    QVERIFY(QDir().mkpath(destination));
    leaveAnUnfinishedRestore(QStringLiteral("done-restore.txa"), destination, 3, true);

    ImportController controller;
    controller.inspect(pathTo(QStringLiteral("done-restore.txa")));
    QTRY_VERIFY(!controller.isInspecting());
    controller.lookForInterruptedRestore(destination);

    // Offering to "finish" a restore that finished would mean writing over a
    // machine that is already the way the user asked for.
    QVERIFY2(!controller.canCarryOn(), "offered to finish a restore that was already done");
}

void CarryOnTest::aRestoreRecordDescribingNothingIsNotOffered() {
    const QString destination = QDir(folder()).filePath(QStringLiteral("into"));
    QVERIFY(QDir().mkpath(destination));
    leaveAnUnfinishedRestore(QStringLiteral("barely-started.txa"), destination, 0);

    ImportController controller;
    controller.inspect(pathTo(QStringLiteral("barely-started.txa")));
    QTRY_VERIFY(!controller.isInspecting());
    controller.lookForInterruptedRestore(destination);

    // Nothing was put in place, so carrying on would save no work at all.
    QVERIFY2(!controller.canCarryOn(), "offered to carry on from nothing");
}

void CarryOnTest::aRestoreIntoADifferentFolderIsNotOffered() {
    const QString destination = QDir(folder()).filePath(QStringLiteral("into"));
    const QString elsewhere = QDir(folder()).filePath(QStringLiteral("elsewhere"));
    QVERIFY(QDir().mkpath(destination));
    QVERIFY(QDir().mkpath(elsewhere));
    leaveAnUnfinishedRestore(QStringLiteral("stopped-restore.txa"), destination);

    ImportController controller;
    controller.inspect(pathTo(QStringLiteral("stopped-restore.txa")));
    QTRY_VERIFY(!controller.isInspecting());

    // The same archive into a different folder is a different restore, and
    // the record says nothing about what is in this one.
    controller.lookForInterruptedRestore(elsewhere);
    QVERIFY2(!controller.canCarryOn(), "offered a record belonging to another folder");

    controller.lookForInterruptedRestore(destination);
    QVERIFY(controller.canCarryOn());
}

void CarryOnTest::theRestoreOfferCanBeTakenUpAndPutDownAgain() {
    const QString destination = QDir(folder()).filePath(QStringLiteral("into"));
    QVERIFY(QDir().mkpath(destination));
    leaveAnUnfinishedRestore(QStringLiteral("stopped-restore.txa"), destination);

    ImportController controller;
    controller.inspect(pathTo(QStringLiteral("stopped-restore.txa")));
    QTRY_VERIFY(!controller.isInspecting());
    controller.lookForInterruptedRestore(destination);
    QVERIFY(controller.canCarryOn());

    QSignalSpy changed(&controller, &ImportController::carryOnChanged);

    controller.carryOn();
    QVERIFY(controller.isCarryingOn());
    QCOMPARE(changed.count(), 1);

    // Asking twice changes nothing and says nothing.
    controller.carryOn();
    QCOMPARE(changed.count(), 1);

    controller.startFresh();
    QVERIFY2(!controller.isCarryingOn(), "still carrying on after being told to start again");
    QVERIFY2(!controller.canCarryOn(), "the offer came back after being turned down");
    QCOMPARE(changed.count(), 2);
}

// The page asks again for every folder the user clicks through. A signal each
// time would redraw the offer for folders that never had one, and in a list
// of a hundred that is a hundred redraws to say "still nothing".
void CarryOnTest::theRestoreOfferOnlySaysSomethingWhenTheAnswerChanges() {
    const QString destination = QDir(folder()).filePath(QStringLiteral("into"));
    const QString elsewhere = QDir(folder()).filePath(QStringLiteral("elsewhere"));
    QVERIFY(QDir().mkpath(destination));
    QVERIFY(QDir().mkpath(elsewhere));
    leaveAnUnfinishedRestore(QStringLiteral("stopped-restore.txa"), destination);

    ImportController controller;
    controller.inspect(pathTo(QStringLiteral("stopped-restore.txa")));
    QTRY_VERIFY(!controller.isInspecting());

    QSignalSpy changed(&controller, &ImportController::carryOnChanged);

    controller.lookForInterruptedRestore(elsewhere);
    controller.lookForInterruptedRestore(elsewhere);
    QCOMPARE(changed.count(), 0);

    controller.lookForInterruptedRestore(destination);
    QCOMPARE(changed.count(), 1);
    controller.lookForInterruptedRestore(destination);
    QCOMPARE(changed.count(), 1);

    controller.lookForInterruptedRestore(elsewhere);
    QCOMPARE(changed.count(), 2);
}

void CarryOnTest::anUnfinishedCaptureIsOffered() {
    leaveAnUnfinishedCapture(QStringLiteral("stopped.txa"));

    ExportController controller;
    QSignalSpy changed(&controller, &ExportController::carryOnChanged);

    controller.lookForInterruptedCapture(folder());

    QVERIFY2(controller.canCarryOn(), "an unfinished capture went unnoticed");
    QCOMPARE(changed.count(), 1);
    QVERIFY2(controller.carryOnText().contains(QStringLiteral("stopped.txa")),
             qPrintable(controller.carryOnText()));
    QVERIFY2(!controller.carryingOn(), "it carried on without being asked to");
}

void CarryOnTest::aFinishedCaptureIsNotOffered() {
    leaveAFinishedCapture(QStringLiteral("whole.txa"));

    ExportController controller;
    controller.lookForInterruptedCapture(folder());

    // The record is right there and says the capture finished. Offering to
    // "carry on" would mean writing into a good archive, which is how a
    // complete capture gets destroyed.
    QVERIFY(QFile::exists(pathTo(QStringLiteral("whole.txa")) + QStringLiteral(".journal")));
    QVERIFY2(!controller.canCarryOn(), "offered to carry on with a finished archive");
}

void CarryOnTest::aFolderWithNothingInItOffersNothing() {
    ExportController controller;
    controller.lookForInterruptedCapture(folder());
    QVERIFY(!controller.canCarryOn());
    QVERIFY(controller.carryOnText().isEmpty());
}

void CarryOnTest::aJournalWithNoBlocksYetIsNotOffered() {
    // The run stopped before a single block reached the drive. There is a
    // record, and it describes nothing: carrying on would save no work at all
    // and would only be a more complicated way of starting again.
    leaveAnUnfinishedCapture(QStringLiteral("barely-started.txa"), false);

    ExportController controller;
    controller.lookForInterruptedCapture(folder());
    QVERIFY2(!controller.canCarryOn(), "offered to carry on from nothing");
}

void CarryOnTest::theNewestUnfinishedCaptureIsTheOneOffered() {
    leaveAnUnfinishedCapture(QStringLiteral("older.txa"));

    // Filesystem timestamps are coarse, so the second one is aged by hand
    // rather than by waiting for the clock.
    const QString older = pathTo(QStringLiteral("older.txa")) + QStringLiteral(".journal");
    QFile aged(older);
    QVERIFY(aged.open(QIODevice::ReadWrite));
    QVERIFY(aged.setFileTime(QDateTime::currentDateTime().addDays(-2),
                             QFileDevice::FileModificationTime));
    aged.close();

    leaveAnUnfinishedCapture(QStringLiteral("newer.txa"));

    ExportController controller;
    controller.lookForInterruptedCapture(folder());

    QVERIFY(controller.canCarryOn());
    QVERIFY2(controller.carryOnText().contains(QStringLiteral("newer.txa")),
             qPrintable(controller.carryOnText()));
}

void CarryOnTest::theOfferCanBeTakenUpAndPutDownAgain() {
    leaveAnUnfinishedCapture(QStringLiteral("stopped.txa"));

    ExportController controller;
    controller.lookForInterruptedCapture(folder());
    QVERIFY(controller.canCarryOn());

    QSignalSpy changed(&controller, &ExportController::carryOnChanged);

    controller.carryOn();
    QVERIFY(controller.carryingOn());
    QCOMPARE(changed.count(), 1);

    // Asking twice changes nothing and says nothing.
    controller.carryOn();
    QCOMPARE(changed.count(), 1);

    controller.startFresh();
    QVERIFY2(!controller.carryingOn(), "still carrying on after being told to start again");
    QVERIFY2(!controller.canCarryOn(), "the offer came back after being turned down");
    QCOMPARE(changed.count(), 2);
}

// Whether the drive can be taken away depends on the capture having finished,
// and a Qt property carries exactly one notify signal. Bind the offer to
// ejectChanged and forget to emit it when the capture ends, and the button is
// correct in every way except that it never appears - which no test of the
// property's value would notice, because the value is right.
void CarryOnTest::finishingACaptureSaysSoToTheEjectOffer() {
    ExportController controller;
    QSignalSpy ejectable(&controller, &ExportController::ejectChanged);
    QSignalSpy finished(&controller, &ExportController::finishedChanged);

    // A capture of an empty home into the workspace. What it finds does not
    // matter; that it finishes, and says so to everything that depends on
    // having finished, does.
    const QString home = QDir(folder()).filePath(QStringLiteral("empty-home"));
    QVERIFY(QDir().mkpath(home));
    const QString previousHome = qEnvironmentVariable("HOME");
    qputenv("HOME", home.toUtf8());

    controller.start(QStringLiteral("documents"), folder(), QStringLiteral("fast"), QString(),
                     false, QStringLiteral("test"), {QStringLiteral("userdata")}, false);

    QVERIFY2(finished.wait(60000), "the capture never finished");
    if (!previousHome.isEmpty()) {
        qputenv("HOME", previousHome.toUtf8());
    }

    QVERIFY2(!ejectable.isEmpty(), "nothing told the eject offer that the capture had finished");
}

// The offer can be correct in every visible way and still do nothing, because
// what it comes down to is one field on the request. A "Finish it" button that
// quietly runs a whole restore instead is worse than no button: under the
// default policy it saves a second copy of everything the interrupted run
// managed, which is the exact harm this feature exists to prevent.
void CarryOnTest::takingTheRestoreOfferUpActuallyCarriesTheRestoreOn() {
    // A real archive of a real little home directory.
    const QString home = QDir(folder()).filePath(QStringLiteral("source-home"));
    const QString documents = home + QStringLiteral("/Documents");
    QVERIFY(QDir().mkpath(documents));
    for (int i = 0; i < 12; ++i) {
        QFile file(documents + QStringLiteral("/doc-%1.bin").arg(i));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write(QByteArray(4096, static_cast<char>('a' + i))) == 4096);
        file.close();
    }

    const QString previousHome = qEnvironmentVariable("HOME");
    qputenv("HOME", home.toUtf8());
    const auto restoreHome = qScopeGuard([&previousHome] {
        if (!previousHome.isEmpty()) {
            qputenv("HOME", previousHome.toUtf8());
        }
    });

    auto platform = platform::PlatformService::create();
    const auto base = platform->knownFolders().base(format::PathTokenId::Documents);
    if (!base || !QString::fromStdString(*base).startsWith(home)) {
        QSKIP("this platform does not resolve its known folders from HOME");
    }

    const QString archive = pathTo(QStringLiteral("for-restore.txa"));
    {
        core::ExportService exporter(*platform);
        core::CancelToken token;
        core::ExportRequest request;
        request.destinationPath = archive;
        request.selection =
            core::ProfileService::profileById(QStringLiteral("documents")).selection;
        request.packaging.preset = format::CompressionPreset::Fast;
        request.packaging.verifyAfterWriting = false;
        const core::ExportReport captured = exporter.run(request, token);
        QVERIFY2(captured.succeeded, qPrintable(captured.errorMessage));
    }

    const QString destination = QDir(folder()).filePath(QStringLiteral("restore-into"));
    QVERIFY(QDir().mkpath(destination));

    ImportController controller;
    controller.inspect(archive);
    QTRY_VERIFY(!controller.isInspecting());

    // A restore that runs out of room part way. Everything but the record of
    // it stops being writable, which is what a full disk looks like from here.
    {
        auto written = std::make_shared<int>(0);
        const QString state =
            QDir(destination).filePath(QString::fromLatin1(core::RollbackWriter::kDirectoryName));
        format::IoHooks hooks;
        hooks.beforeWrite = [destination, state, written](
                                const std::filesystem::path& path, std::uint64_t,
                                std::size_t) -> std::optional<format::Error> {
            const QString touched = QString::fromStdString(path.string());
            if (touched.startsWith(state) || !touched.startsWith(destination)) {
                return std::nullopt;
            }
            if (++*written <= 5) {
                return std::nullopt;
            }
            format::Error full{format::ErrorCode::IoError, "the disk stopped accepting writes"};
            full.systemCode = ENOSPC;
            return full;
        };
        const format::ScopedIoHooks installed(std::move(hooks));

        QSignalSpy finished(&controller, &ImportController::finishedChanged);
        controller.start(QString(), QStringLiteral("keep-both"), false, false, destination);
        QVERIFY2(finished.wait(60000), "the interrupted restore never finished");
    }
    QVERIFY2(!controller.succeeded(), "the disk refused every write and the restore succeeded");
    QVERIFY2(controller.report().canBeCarriedOn, "nothing was left to carry on from");

    controller.reset();
    controller.lookForInterruptedRestore(destination);
    QVERIFY2(controller.canCarryOn(), "the interrupted restore was not offered");

    controller.carryOn();
    QVERIFY(controller.isCarryingOn());

    QSignalSpy finishedAgain(&controller, &ImportController::finishedChanged);
    controller.start(QString(), QStringLiteral("keep-both"), false, false, destination);
    QVERIFY2(finishedAgain.wait(60000), "the second restore never finished");

    QVERIFY2(controller.succeeded(), qPrintable(controller.errorMessage()));
    QVERIFY2(controller.report().resumed,
             "the offer was taken up and the restore started again anyway");
    QVERIFY2(controller.report().filesCarriedOver > 0,
             "it claimed to carry on and settled every item a second time");
}

QTEST_MAIN(CarryOnTest)
#include "CarryOnTest.moc"
