// A restore the disk interrupted, finished rather than done again.
//
// Running an interrupted restore a second time was already safe: a file that
// is byte for byte what the archive holds is recognised and left alone. What
// it was not is cheap, and under the default policy for a home directory it
// was not even correct. "Keep both" saves a colliding file under a name it
// invents, and a second run that does not know which name the first one
// invented invents another - so the person who wanted their documents back
// gets some of them twice.
//
// These tests are about the record that closes both gaps: what it lets a
// second run skip, what it must refuse to carry on from, and - the one that
// matters most - that the tree at the end is the same tree an uninterrupted
// restore would have produced.

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>

#include "core/services/ExportService.h"
#include "core/services/ImportService.h"
#include "core/services/ProfileService.h"
#include "core/services/RollbackWriter.h"
#include "format/Container.h"
#include "format/IoHooks.h"
#include "format/PathToken.h"
#include "format/RestoreJournal.h"
#include "platform/PlatformService.h"

using namespace transmit;

namespace {

QByteArray noise(int seed, int length) {
    QByteArray out;
    out.resize(length);
    quint32 state = static_cast<quint32>(seed) * 2654435761u + 1u;
    for (int i = 0; i < length; ++i) {
        state = state * 1664525u + 1013904223u;
        out[i] = static_cast<char>((state >> 24) & 0xFFu);
    }
    return out;
}

}  // namespace

class ResumeRestoreTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void aRestoreTheDiskInterruptedIsCarriedOn();
    void carryingOnDoesNotSaveASecondCopyOfWhatIsAlreadyThere();
    void aFinishedRestoreLeavesNoRecordBehind();
    void carryingOnWithDifferentSettingsIsRefused();
    void withoutTheRecordEveryItemIsSettledAgain();

private:
    [[nodiscard]] QString sourceHome() const { return workspace_.filePath("home"); }
    [[nodiscard]] QString archivePath() const { return workspace_.filePath("capture.txa"); }
    [[nodiscard]] QString restoreInto() const { return restored_.path(); }

    [[nodiscard]] std::filesystem::path journalPath() const;

    /// Every file under the destination, by name. Which folder the token
    /// layout puts them in is not what these tests are about, and hard-coding
    /// it would make them fail for a reason they are not asking about.
    [[nodiscard]] QStringList restoredFileNames() const;

    /// Restores into a fresh folder, optionally with the disk refusing writes
    /// once `failAfterFiles` files have been written.
    [[nodiscard]] core::ImportReport restore(
        bool resume, int failAfterFiles = -1, bool keepJournal = true,
        core::ConflictPolicy policy = core::ConflictPolicy::KeepBoth);

    QTemporaryDir workspace_;
    QTemporaryDir restored_;
    QString originalHome_;
    std::unique_ptr<platform::PlatformService> platform_;
    QHash<QString, QByteArray> expected_;
    bool usable_ = false;
};

void ResumeRestoreTest::initTestCase() {
    QVERIFY(workspace_.isValid());
    QVERIFY(restored_.isValid());

    originalHome_ = qEnvironmentVariable("HOME");
    for (const char* name :
         {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_DOCUMENTS_DIR"}) {
        qunsetenv(name);
    }
    qputenv("HOME", sourceHome().toUtf8());
    platform_ = platform::PlatformService::create();

    const format::PathTokenMap folders = platform_->knownFolders();
    const auto documents = folders.base(format::PathTokenId::Documents);
    usable_ = documents && QString::fromStdString(*documents).startsWith(sourceHome());
    if (!usable_) {
        QSKIP("this platform does not resolve its known folders from HOME");
    }

    // A source tree, captured once. Every test here restores that one archive.
    const QString documentsFolder = sourceHome() + "/Documents";
    QVERIFY(QDir().mkpath(documentsFolder));
    for (int i = 0; i < 24; ++i) {
        const QString name = QStringLiteral("file-%1.bin").arg(i, 3, 10, QLatin1Char('0'));
        const QByteArray content = noise(i + 1, 8 * 1024);
        QFile file(documentsFolder + QLatin1Char('/') + name);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(content), content.size());
        file.close();
        expected_.insert(name, content);
    }

    core::ExportService exporter(*platform_);
    core::CancelToken token;
    core::ExportRequest request;
    request.destinationPath = archivePath();
    request.selection = core::ProfileService::profileById(QStringLiteral("documents")).selection;
    request.packaging.preset = format::CompressionPreset::Fast;
    request.packaging.verifyAfterWriting = false;
    const core::ExportReport captured = exporter.run(request, token);
    QVERIFY2(captured.succeeded, qPrintable(captured.errorMessage));
}

void ResumeRestoreTest::cleanupTestCase() {
    if (!originalHome_.isEmpty()) {
        qputenv("HOME", originalHome_.toUtf8());
    }
}

void ResumeRestoreTest::init() {
    if (!usable_) {
        QSKIP("this platform does not resolve its known folders from HOME");
    }
    // A fresh destination for each case: a record or a file left by an earlier
    // one would be the answer rather than what this case arranged.
    QDir(restoreInto()).removeRecursively();
    QVERIFY(QDir().mkpath(restoreInto()));
}

std::filesystem::path ResumeRestoreTest::journalPath() const {
    auto reader = format::ArchiveReader::open(std::filesystem::path(archivePath().toStdString()));
    Q_ASSERT(reader.operator bool());
    const QString state =
        QDir(restoreInto()).filePath(QString::fromLatin1(core::RollbackWriter::kDirectoryName));
    return format::RestoreJournal::pathFor(std::filesystem::path(state.toStdString()),
                                           (*reader)->uuid());
}

QStringList ResumeRestoreTest::restoredFileNames() const {
    QStringList names;
    QDirIterator walk(restoreInto(), QDir::Files, QDirIterator::Subdirectories);
    while (walk.hasNext()) {
        const QString path = walk.next();
        // The undo point and the record of the run are not restored files.
        if (path.contains(QStringLiteral("/.transmit/"))) {
            continue;
        }
        names << QFileInfo(path).fileName();
    }
    names.sort();
    return names;
}

core::ImportReport ResumeRestoreTest::restore(bool resume, int failAfterFiles, bool keepJournal,
                                              core::ConflictPolicy policy) {
    core::ImportService importer(*platform_);
    core::CancelToken token;

    core::ImportRequest request;
    request.archivePath = archivePath();
    request.destinationOverride = restoreInto();
    request.conflictPolicy = policy;
    request.createRollback = false;
    request.durableWrites = false;
    request.keepJournal = keepJournal;
    request.resume = resume;

    if (failAfterFiles < 0) {
        return importer.run(request, token);
    }

    // The disk stops accepting writes once enough files have landed. Restores
    // write each file whole, so counting writes into the destination counts
    // files - and the journal beside them has to keep working, or the test
    // would be about losing the record rather than losing the run.
    const QString destination = restoreInto();
    const QString state =
        QDir(destination).filePath(QString::fromLatin1(core::RollbackWriter::kDirectoryName));
    auto written = std::make_shared<int>(0);

    format::IoHooks hooks;
    hooks.beforeWrite = [destination, state, failAfterFiles, written](
                            const std::filesystem::path& path, std::uint64_t,
                            std::size_t) -> std::optional<format::Error> {
        const QString touched = QString::fromStdString(path.string());
        if (touched.startsWith(state) || !touched.startsWith(destination)) {
            return std::nullopt;
        }
        if (++*written <= failAfterFiles) {
            return std::nullopt;
        }
        format::Error full{format::ErrorCode::IoError, "the disk stopped accepting writes"};
        full.systemCode = ENOSPC;
        return full;
    };
    const format::ScopedIoHooks installed(std::move(hooks));
    return importer.run(request, token);
}

void ResumeRestoreTest::aRestoreTheDiskInterruptedIsCarriedOn() {
    const core::ImportReport stopped = restore(false, 10);
    QVERIFY2(!stopped.succeeded, "the disk refused every write and the restore still succeeded");
    QVERIFY2(stopped.canBeCarriedOn, "nothing was left to carry on from");
    QVERIFY2(std::filesystem::exists(journalPath()), "the record was not written");

    const core::ImportReport finished = restore(true);
    QVERIFY2(finished.succeeded, qPrintable(finished.errorMessage));
    QVERIFY2(finished.resumed, "the second run started afresh instead of carrying on");
    QVERIFY2(finished.filesCarriedOver > 0, "nothing was carried over from the first run");
    QVERIFY2(finished.filesCarriedOver < static_cast<quint64>(expected_.size()),
             "the first run supposedly restored everything, so nothing was interrupted");
    QVERIFY2(!std::filesystem::exists(journalPath()),
             "the record outlived the restore that finished");

    // The whole point: every file, with the right bytes, whichever run wrote
    // it - and no others.
    const QStringList names = restoredFileNames();
    QCOMPARE(names.size(), expected_.size());

    QDirIterator walk(restoreInto(), QDir::Files, QDirIterator::Subdirectories);
    int checked = 0;
    while (walk.hasNext()) {
        const QString path = walk.next();
        const auto found = expected_.constFind(QFileInfo(path).fileName());
        if (found == expected_.constEnd()) {
            continue;
        }
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
        QCOMPARE(file.readAll(), found.value());
        ++checked;
    }
    QCOMPARE(checked, expected_.size());
}

// The reason the record exists.
//
// A restore into a folder that already has files of the same name saves the
// archive's copy alongside, under a name it invents. That name is the thing
// only the record remembers: recognising a file as "already exactly right"
// cannot help here, because what is at the obvious path is somebody else's
// file and is supposed to stay. Without the record the resumed run meets its
// own earlier work, does not recognise it, and invents a second name for it.
void ResumeRestoreTest::carryingOnDoesNotSaveASecondCopyOfWhatIsAlreadyThere() {
    // Files already in the destination, under the archive's names, holding
    // something else. This is the ordinary case of restoring onto a machine
    // that is already in use.
    const QString folder =
        QDir(restoreInto())
            .filePath(QString::fromUtf8(
                format::tokenName(format::PathTokenId::Documents).data(),
                static_cast<qsizetype>(format::tokenName(format::PathTokenId::Documents).size())));
    QVERIFY(QDir().mkpath(folder));
    for (auto it = expected_.constBegin(); it != expected_.constEnd(); ++it) {
        QFile mine(QDir(folder).filePath(it.key()));
        QVERIFY(mine.open(QIODevice::WriteOnly));
        mine.write(QByteArrayLiteral("this was already here"));
        mine.close();
    }

    const core::ImportReport stopped = restore(false, 10);
    QVERIFY(!stopped.succeeded);
    QVERIFY(stopped.canBeCarriedOn);

    // The interrupted run saved what it managed alongside, under invented
    // names. Without that there is nothing for the second run to duplicate and
    // this test would prove nothing.
    const QStringList afterFirst = restoredFileNames();
    const int savedAlongside =
        static_cast<int>(std::count_if(afterFirst.begin(), afterFirst.end(), [](const QString& n) {
            return n.contains(QLatin1Char('~'));
        }));
    QVERIFY2(savedAlongside > 0, "the interrupted run saved nothing alongside");

    const core::ImportReport finished = restore(true);
    QVERIFY2(finished.succeeded, qPrintable(finished.errorMessage));

    // Each archive file is here exactly once, beside the one that was already
    // there. A second name for a file the first run had already saved would
    // show up as "~2" - or simply as more files than there should be.
    const QStringList names = restoredFileNames();
    for (const QString& name : names) {
        QVERIFY2(!name.contains(QStringLiteral("~2")),
                 qPrintable(QStringLiteral("a second copy was saved: %1").arg(name)));
    }
    QCOMPARE(names.size(), expected_.size() * 2);

    // And what was already there is untouched: keeping both means keeping both.
    for (auto it = expected_.constBegin(); it != expected_.constEnd(); ++it) {
        QFile mine(QDir(folder).filePath(it.key()));
        QVERIFY2(mine.open(QIODevice::ReadOnly), qPrintable(it.key()));
        QCOMPARE(mine.readAll(), QByteArrayLiteral("this was already here"));
    }
}

void ResumeRestoreTest::aFinishedRestoreLeavesNoRecordBehind() {
    const core::ImportReport done = restore(false);
    QVERIFY2(done.succeeded, qPrintable(done.errorMessage));

    // A record beside a finished restore would offer to carry on with one that
    // has nothing left to do.
    QVERIFY2(!std::filesystem::exists(journalPath()), "the record outlived the restore");
    QVERIFY(!done.canBeCarriedOn);
    QVERIFY(!done.resumed);
}

void ResumeRestoreTest::carryingOnWithDifferentSettingsIsRefused() {
    const core::ImportReport stopped = restore(false, 10);
    QVERIFY(!stopped.succeeded);
    QVERIFY(stopped.canBeCarriedOn);

    // The same archive into the same folder, but told to overwrite rather than
    // keep both. The record describes where the first run put things under a
    // policy this one is not following, so carrying on from it would put files
    // where this run did not intend them.
    const core::ImportReport refused = restore(true, -1, true, core::ConflictPolicy::Overwrite);
    QVERIFY2(!refused.succeeded, "carried on from a restore that was made differently");
    QVERIFY2(refused.errorMessage.contains(QStringLiteral("different restore")),
             qPrintable(refused.errorMessage));
}

void ResumeRestoreTest::withoutTheRecordEveryItemIsSettledAgain() {
    const core::ImportReport stopped = restore(false, 10, false);
    QVERIFY(!stopped.succeeded);
    QVERIFY2(!stopped.canBeCarriedOn, "offered to carry on without a record of what was done");
    QVERIFY(!std::filesystem::exists(journalPath()));

    // Still correct, just not cheap: the files already there are recognised as
    // already right rather than treated as conflicts.
    const core::ImportReport again = restore(false);
    QVERIFY2(again.succeeded, qPrintable(again.errorMessage));
    QCOMPARE(again.filesCarriedOver, 0U);

    QCOMPARE(restoredFileNames().size(), expected_.size());
}

QTEST_MAIN(ResumeRestoreTest)
#include "ResumeRestoreTest.moc"
