// A capture the drive interrupted, finished rather than done again.
//
// The unit tests cover the machinery - the journal's format, truncating a set,
// reopening a writer. This is the thing itself: a real capture of a real home
// directory that fails part way because the drive stops accepting writes, and
// a second run that picks it up and produces an archive holding every file,
// with the right bytes in each.
//
// The interruption is injected rather than waited for. Pulling a stick out at
// the right moment is not something a test can arrange, but the failure it
// produces is exactly a write that stops working, which is.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "core/services/ExportService.h"
#include "core/services/ImportService.h"
#include "core/services/ProfileService.h"
#include "format/Container.h"
#include "format/IoHooks.h"
#include "format/TransferJournal.h"
#include "platform/PlatformService.h"

using namespace transmit;

namespace {

/// Bytes that do not compress, so the archive is large enough for the
/// interruption to land in the middle of it rather than past the end.
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

class ResumeCaptureTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void aCaptureTheDriveInterruptedIsKeptAndCarriedOn();
    void carryingOnAfterTheFilesChangedIsRefused();
    void carryingOnWithACaptureThatFinishedIsRefused();
    void withoutTheJournalNothingIsLeftToCarryOn();

private:
    [[nodiscard]] QString sourceHome() const { return workspace_.filePath("home"); }
    [[nodiscard]] QString documentsFolder() const { return sourceHome() + "/Documents"; }
    [[nodiscard]] core::CaptureSelection documentsSelection() const {
        return core::ProfileService::profileById(QStringLiteral("documents")).selection;
    }

    /// Writes the fixture files and remembers what is in each.
    void buildSourceTree();

    /// Runs a capture, optionally with the drive giving up after `failAfter`
    /// bytes have been written to the archive.
    [[nodiscard]] core::ExportReport capture(const QString& destination, bool resume,
                                             qint64 failAfter = -1, bool keepJournal = true);

    QTemporaryDir workspace_;
    QString originalHome_;
    std::unique_ptr<platform::PlatformService> platform_;
    QHash<QString, QByteArray> expected_;
    bool usable_ = false;
};

void ResumeCaptureTest::initTestCase() {
    QVERIFY(workspace_.isValid());

    originalHome_ = qEnvironmentVariable("HOME");
    for (const char* name :
         {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_DOCUMENTS_DIR"}) {
        qunsetenv(name);
    }
    qputenv("HOME", sourceHome().toUtf8());
    platform_ = platform::PlatformService::create();

    // The same reason the round-trip suite gives: a platform that does not
    // resolve its folders from HOME would run this against the real account.
    const format::PathTokenMap folders = platform_->knownFolders();
    const auto documents = folders.base(format::PathTokenId::Documents);
    usable_ = documents && QString::fromStdString(*documents).startsWith(sourceHome());
    if (!usable_) {
        QSKIP("this platform does not resolve its known folders from HOME");
    }

    buildSourceTree();
}

void ResumeCaptureTest::cleanupTestCase() {
    if (!originalHome_.isEmpty()) {
        qputenv("HOME", originalHome_.toUtf8());
    }
}

void ResumeCaptureTest::buildSourceTree() {
    QVERIFY(QDir().mkpath(documentsFolder()));

    // Enough files, and enough incompressible bytes in them, that the capture
    // fills several blocks: an interruption inside the first block would leave
    // nothing recorded and prove nothing.
    for (int i = 0; i < 40; ++i) {
        const QString name = QStringLiteral("file-%1.bin").arg(i, 3, 10, QLatin1Char('0'));
        const QByteArray content = noise(i + 1, 24 * 1024);

        QFile file(documentsFolder() + QLatin1Char('/') + name);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(content), content.size());
        file.close();

        expected_.insert(name, content);
    }
}

core::ExportReport ResumeCaptureTest::capture(const QString& destination, bool resume,
                                              qint64 failAfter, bool keepJournal) {
    core::ExportService exporter(*platform_);
    core::CancelToken token;

    core::ExportRequest request;
    request.destinationPath = destination;
    request.selection = documentsSelection();
    request.packaging.preset = format::CompressionPreset::Fast;
    // Small blocks, so the forty files land in many of them and the
    // interruption falls between two rather than inside the only one.
    request.packaging.solidBlockSize = 64 * 1024;
    request.packaging.keepJournal = keepJournal;
    request.packaging.verifyAfterWriting = false;
    request.resume = resume;

    if (failAfter < 0) {
        return exporter.run(request, token);
    }

    // The drive stops accepting writes to the archive part way through, which
    // is what a full stick and an unplugged one both look like from here.
    const std::string archive = destination.toStdString();
    format::IoHooks hooks;
    hooks.beforeWrite = [archive, failAfter](const std::filesystem::path& path,
                                             std::uint64_t offset,
                                             std::size_t) -> std::optional<format::Error> {
        if (path.string().rfind(archive, 0) != 0) {
            return std::nullopt;  // the journal beside it still works
        }
        if (offset < static_cast<std::uint64_t>(failAfter)) {
            return std::nullopt;
        }
        format::Error full{format::ErrorCode::IoError, "the drive stopped accepting writes"};
        full.systemCode = ENOSPC;
        return full;
    };
    const format::ScopedIoHooks installed(std::move(hooks));
    return exporter.run(request, token);
}

void ResumeCaptureTest::aCaptureTheDriveInterruptedIsKeptAndCarriedOn() {
    const QString destination = workspace_.filePath("carried-on.txa");

    const core::ExportReport stopped = capture(destination, false, 300 * 1024);
    QVERIFY2(!stopped.succeeded, "the drive refused every write and the capture still finished");
    QVERIFY2(stopped.canBeCarriedOn, qPrintable(stopped.errorMessage));

    // What was written is still there, and so is the record of it. Without
    // both, there is nothing to carry on with.
    QVERIFY2(QFileInfo::exists(destination), "the half-written archive was deleted");
    const QString journal = destination + QStringLiteral(".journal");
    QVERIFY2(QFileInfo::exists(journal), "the journal was deleted with it");

    const core::ExportReport finished = capture(destination, true);
    QVERIFY2(finished.succeeded, qPrintable(finished.errorMessage));
    QVERIFY2(finished.resumed, "the second run started again instead of carrying on");
    QVERIFY2(finished.filesCarriedOver > 0, "nothing was carried over from the first run");
    QVERIFY2(finished.filesCarriedOver < static_cast<quint64>(expected_.size()),
             "the first run supposedly captured everything, so nothing was interrupted");
    QVERIFY2(!QFileInfo::exists(journal), "the journal outlived the capture that finished");

    // The whole point: every file, with the right bytes, whichever run wrote
    // it. A resumed archive that opens and has one file shifted by a block
    // header would pass every check but this one.
    const QString restored = workspace_.filePath("restored");
    QVERIFY(QDir().mkpath(restored));

    auto reader = format::ArchiveReader::open(std::filesystem::path(destination.toStdString()));
    QVERIFY2(reader.operator bool(), "the resumed archive could not be opened");
    auto manifest = (*reader)->manifest();
    QVERIFY2(manifest.operator bool(), "the resumed archive has no readable manifest");

    int checked = 0;
    for (const format::ManifestEntry& entry : (*manifest)->entries) {
        if (entry.type != format::EntryType::File || entry.size == 0) {
            continue;
        }
        const QString name = QFileInfo(QString::fromStdString(entry.path.relative)).fileName();
        const auto found = expected_.constFind(name);
        if (found == expected_.constEnd()) {
            continue;
        }
        auto content = (*reader)->readEntry(entry);
        QVERIFY2(content.operator bool(), qPrintable(name));
        const QByteArray got(reinterpret_cast<const char*>(content->data()),
                             static_cast<qsizetype>(content->size()));
        QCOMPARE(got, found.value());
        ++checked;
    }
    QCOMPARE(checked, expected_.size());
}

void ResumeCaptureTest::carryingOnAfterTheFilesChangedIsRefused() {
    const QString destination = workspace_.filePath("moved-on.txa");

    const core::ExportReport stopped = capture(destination, false, 300 * 1024);
    QVERIFY(!stopped.succeeded);
    QVERIFY(stopped.canBeCarriedOn);

    // One file, changed. The entries already recorded describe the machine as
    // it was, and nothing here can tell whether the difference matters - so
    // the archive must not become half of one state and half of another.
    {
        QFile file(documentsFolder() + QStringLiteral("/file-000.bin"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(noise(999, 24 * 1024 + 1));
        file.close();
    }

    const core::ExportReport refused = capture(destination, true);
    QVERIFY2(!refused.succeeded, "carried on over a source that had changed");
    QVERIFY2(refused.errorMessage.contains(QStringLiteral("different capture")),
             qPrintable(refused.errorMessage));

    // Put it back, so the tests after this one see the tree they expect.
    QFile file(documentsFolder() + QStringLiteral("/file-000.bin"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(expected_.value(QStringLiteral("file-000.bin")));
    file.close();
}

void ResumeCaptureTest::carryingOnWithACaptureThatFinishedIsRefused() {
    const QString destination = workspace_.filePath("already-done.txa");

    const core::ExportReport done = capture(destination, false);
    QVERIFY2(done.succeeded, qPrintable(done.errorMessage));

    // Nothing to carry on with, and the archive is whole. Writing into it
    // again would destroy a good capture to redo work that is finished.
    const core::ExportReport again = capture(destination, true);
    QVERIFY2(!again.succeeded, "wrote into an archive that was already complete");
    QVERIFY(QFileInfo::exists(destination));
}

void ResumeCaptureTest::withoutTheJournalNothingIsLeftToCarryOn() {
    const QString destination = workspace_.filePath("no-journal.txa");

    const core::ExportReport stopped = capture(destination, false, 300 * 1024, false);
    QVERIFY(!stopped.succeeded);
    QVERIFY2(!stopped.canBeCarriedOn, "offered to carry on without a record of what was written");

    // A half-written archive nobody can read is worse than none, so with no
    // journal to make sense of it, it goes.
    QVERIFY2(!QFileInfo::exists(destination), "a half archive was left with nothing to explain it");
    QVERIFY(!QFileInfo::exists(destination + QStringLiteral(".journal")));
}

QTEST_MAIN(ResumeCaptureTest)
#include "ResumeCaptureTest.moc"
