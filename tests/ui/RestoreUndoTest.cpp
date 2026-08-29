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
    void aRestoreThatCouldNotWriteSaysSoAndStaysUndoable();

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

    // Windows and macOS resolve the known folders through their own shell
    // APIs, which do not follow HOME - so on those the capture below would
    // read the machine's real Documents folder. Skipping is the honest answer:
    // the undo logic these tests are about is not platform-specific, and
    // reading somebody's actual documents to prove it is not a trade worth
    // making.
    const auto documents = platform->knownFolders().base(format::PathTokenId::Documents);
    if (!documents || !QString::fromStdString(*documents).startsWith(home)) {
        return {};
    }

    core::ExportService service(*platform);

    core::ExportRequest request;
    request.destinationPath = workspace_->filePath(QStringLiteral("capture.txa"));
    // Named rather than positional: CaptureRoot has gained fields, and a
    // brace list would silently put the next new one in the wrong slot.
    core::CaptureRoot documentsRoot;
    documentsRoot.token = format::PathTokenId::Documents;
    documentsRoot.domain = format::DomainId::UserData;
    request.selection.roots.push_back(documentsRoot);

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
    if (archive.isEmpty()) {
        QSKIP("this platform does not resolve its known folders from HOME");
    }

    app::ImportController controller;
    QVERIFY2(!controller.canUndo(), "there is nothing to undo before a restore has run");

    const QString destination = workspace_->filePath(QStringLiteral("restored"));
    restoreInto(controller, archive, destination);

    QVERIFY2(controller.canUndo(), "a completed restore must be undoable");
    QVERIFY(!controller.undoDescription().isEmpty());
}

void RestoreUndoTest::keepingTheRestoreClearsUpAfterItself() {
    const QString archive = captureSomething();
    if (archive.isEmpty()) {
        QSKIP("this platform does not resolve its known folders from HOME");
    }

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
    if (archive.isEmpty()) {
        QSKIP("this platform does not resolve its known folders from HOME");
    }

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
/// A restore that could not write reported success and exit 0, which made
/// the most common real failure - a destination that turns out not to be
/// writable - invisible to every caller. Worse, undo was gated on that
/// same flag, so a half-changed machine had the undo button switched off.
void RestoreUndoTest::aRestoreThatCouldNotWriteSaysSoAndStaysUndoable() {
    const QString archive = captureSomething();
    if (archive.isEmpty()) {
        QSKIP("this platform does not resolve its known folders from HOME");
    }

    // The destination is an ordinary file, so every attempt to create a
    // folder under it fails. Permission bits would have been the obvious
    // way to arrange this and root ignores them; a file is a file for
    // everybody.
    const QString destination = workspace_->filePath(QStringLiteral("not-a-folder"));
    QFile blocker(destination);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.write("in the way\n");
    blocker.close();

    app::ImportController controller;
    restoreInto(controller, archive, destination);

    QVERIFY2(controller.isFinished(), "the restore should have finished rather than hung");
    QVERIFY2(!controller.succeeded(), "a restore that wrote nothing must not report success");
    QVERIFY2(!controller.errorMessage().isEmpty(), "it has to say what went wrong");
}

#include "RestoreUndoTest.moc"
