// Offering to finish a capture the drive interrupted.
//
// The command line can already carry one on. The window is where most people
// will meet the situation, and there the offer has to appear by itself: nobody
// who has just watched a capture die is going to go looking for a flag. So the
// controller reads the record beside each archive in the chosen folder and
// says what it found - and, just as importantly, says nothing when there is
// nothing to say.

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "app/ExportController.h"
#include "core/utils/Conversions.h"
#include "format/Container.h"
#include "format/TransferJournal.h"

using namespace transmit;
using transmit::app::ExportController;

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

private:
    /// Writes an archive and the record beside it, as an interrupted run
    /// leaves them: no footer, and a journal saying what reached the drive.
    void leaveAnUnfinishedCapture(const QString& name, bool withABlock = true);

    /// Writes a whole archive and leaves its record beside it, marked
    /// finished. A capture normally deletes that record, but it can outlive
    /// the deletion - a drive that was pulled out between the two - and what
    /// it says has to be believed either way.
    void leaveAFinishedCapture(const QString& name);

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

QTEST_MAIN(CarryOnTest)
#include "CarryOnTest.moc"
