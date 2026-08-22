#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "app/ImportController.h"
#include "core/continuity/ContinuityTypes.h"
#include "core/rewrite/RewritePlan.h"
#include "core/services/ExportService.h"
#include "platform/PlatformService.h"

using namespace transmit;

/// Every restore writes an undo point before it touches anything. These tests
/// hold the controller to the two answers a person can give afterwards: put it
/// back, or keep it - and in both cases leave nothing of Transmit's behind.
class RestoreUndoTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void aRestoreOffersAnUndoPoint();
    void keepingTheRestoreClearsUpAfterItself();
    void undoingPutsTheMachineBack();

private:
    /// Captures a small home directory and returns the archive path.
    [[nodiscard]] QString captureSomething();

    /// Restores into a directory of its own and waits for it to finish.
    void restoreInto(app::ImportController& controller, const QString& archive,
                     const QString& destination);

    /// Where notes.txt ended up. Restoring into a folder of the user's
    /// choosing groups things under the known folder they came from, so the
    /// test looks for the file rather than assuming the layout.
    [[nodiscard]] static QString findRestoredNote(const QString& destination);

    std::unique_ptr<QTemporaryDir> workspace_;
};

void RestoreUndoTest::init() {
    workspace_ = std::make_unique<QTemporaryDir>();
    QVERIFY(workspace_->isValid());
}

void RestoreUndoTest::cleanup() {
    workspace_.reset();
}

QString RestoreUndoTest::captureSomething() {
    const QString home = workspace_->filePath(QStringLiteral("home"));
    QDir().mkpath(home + QStringLiteral("/Documents"));

    QFile note(home + QStringLiteral("/Documents/notes.txt"));
    if (!note.open(QIODevice::WriteOnly)) {
        return {};
    }
    note.write("something worth carrying\n");
    note.close();

    qputenv("HOME", home.toUtf8());

    auto platform = platform::PlatformService::create();
    core::ExportService service(*platform);

    core::ExportRequest request;
    request.destinationPath = workspace_->filePath(QStringLiteral("capture.txa"));
    request.selection.roots.push_back(core::CaptureRoot{
        format::PathTokenId::Documents, {}, format::DomainId::UserData, {}, true, {}});

    core::CancelToken token;
    const core::ExportReport report = service.run(request, token, nullptr);
    return report.succeeded ? request.destinationPath : QString();
}

void RestoreUndoTest::restoreInto(app::ImportController& controller, const QString& archive,
                                  const QString& destination) {
    controller.inspect(archive, QString());

    QSignalSpy finished(&controller, &app::ImportController::finishedChanged);
    controller.start(QString(), QStringLiteral("overwrite"), /*dryRun=*/false,
                     /*verifyFirst=*/false, destination);
    QVERIFY(finished.wait(30000));
}

QString RestoreUndoTest::findRestoredNote(const QString& destination) {
    QDirIterator walker(destination, {QStringLiteral("notes.txt")}, QDir::Files,
                        QDirIterator::Subdirectories);
    return walker.hasNext() ? walker.next() : QString();
}

void RestoreUndoTest::aRestoreOffersAnUndoPoint() {
    const QString archive = captureSomething();
    QVERIFY2(!archive.isEmpty(), "the capture did not produce an archive");

    app::ImportController controller;
    QVERIFY2(!controller.canUndo(), "there is nothing to undo before a restore has run");

    const QString destination = workspace_->filePath(QStringLiteral("restored"));
    restoreInto(controller, archive, destination);

    QVERIFY2(controller.canUndo(), "a completed restore must be undoable");
    QVERIFY(!controller.undoDescription().isEmpty());
}

void RestoreUndoTest::keepingTheRestoreClearsUpAfterItself() {
    const QString archive = captureSomething();
    QVERIFY(!archive.isEmpty());

    app::ImportController controller;
    const QString destination = workspace_->filePath(QStringLiteral("restored"));
    restoreInto(controller, archive, destination);
    QVERIFY(controller.canUndo());

    const QString undoDirectory =
        destination + QLatin1Char('/') + QLatin1String(core::RollbackWriter::kDirectoryName);
    QVERIFY2(QFileInfo::exists(undoDirectory), "the restore should have left an undo point");

    controller.keepLastRestore();

    QVERIFY2(!controller.canUndo(), "the answer has been given; it cannot be given twice");
    QVERIFY2(!QFileInfo::exists(undoDirectory),
             "keeping the restore should take Transmit's own files away with it");

    // What the user actually asked for is untouched.
    QVERIFY2(!findRestoredNote(destination).isEmpty(),
             "keeping the restore must not remove anything it restored");
}

void RestoreUndoTest::undoingPutsTheMachineBack() {
    const QString archive = captureSomething();
    QVERIFY(!archive.isEmpty());

    app::ImportController controller;
    const QString destination = workspace_->filePath(QStringLiteral("restored"));
    restoreInto(controller, archive, destination);
    QVERIFY(controller.canUndo());

    const QString restored = findRestoredNote(destination);
    QVERIFY2(!restored.isEmpty(), "the restore should have produced the note");

    QSignalSpy undoChanged(&controller, &app::ImportController::undoChanged);
    controller.undoLastRestore();
    QVERIFY(undoChanged.wait(30000));

    // The undo runs on a worker; wait for it to report rather than for one tick.
    while (controller.isUndoing()) {
        QVERIFY(undoChanged.wait(30000));
    }

    QVERIFY2(!QFileInfo::exists(restored),
             "a file the restore created should be gone once it is undone");
    QVERIFY2(!controller.canUndo(), "the undo point has been used");
    QVERIFY2(!controller.undoSummary().isEmpty(), "the user has to be told what happened");
}

QTEST_MAIN(RestoreUndoTest)
#include "RestoreUndoTest.moc"
