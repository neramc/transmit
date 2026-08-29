#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "core/services/ExportService.h"
#include "core/services/ImportService.h"
#include "core/services/ProfileService.h"
#include "core/services/RollbackWriter.h"
#include "core/utils/Conversions.h"
#include "platform/PlatformService.h"

using namespace transmit;

/// End-to-end coverage of the thing the application exists to do: capture a
/// home directory, restore it somewhere else, and confirm that what comes back
/// is what went in - including when the target is a different operating system.
class ContinuityRoundTripTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void capturesAndRestoresUserFiles();
    void deduplicatesRepeatedContent();
    void splitsAcrossVolumesAndReadsThemBack();
    void encryptsWhenGivenAPassphrase();
    void refusesCredentialsWithoutAPassphrase();
    void renamesFilesThatCollideOnACaseBlindTarget();
    void reportsWhatARestoreWouldDoWithoutWriting();
    void honoursTheSkipConflictPolicy();
    void aCancelledCaptureLeavesNoArchiveBehind();
    void theArchiveFolderIsMadeButNotInvented();
    void aRestoreCanBeUndone();
    void settingsOfAnUnknownProgramStillTravel();
    void overlappingRootsCaptureAFileOnlyOnce();
    void foldersAreRestoredBeforeWhatGoesInsideThem();
    void aFolderThatArrivesReadOnlyStillGetsItsContents();
    void cleanupTestCase();

private:
    /// Builds a home directory with the shapes that break naive tools: nested
    /// folders, duplicate content, an awkward name, and - where the filesystem
    /// can hold one - a case-only collision.
    void buildSourceTree(const QString& home);

    /// Whether this filesystem tells Notes.txt from notes.txt.
    ///
    /// macOS formats its boot volume case-insensitively by default, so the
    /// pair the collision tests want simply cannot exist there. Detected by
    /// asking rather than by looking at the platform name: a case-sensitive
    /// volume on macOS and a case-insensitive one on Linux are both perfectly
    /// possible.
    [[nodiscard]] static bool distinguishesCase(const QString& directory);
    [[nodiscard]] core::CaptureSelection documentsSelection() const;

    /// Where this platform actually keeps a program's configuration and data,
    /// which is where the fixture has to put the unknown program's files for
    /// the capture to find them at all.
    [[nodiscard]] QString baseFor(format::PathTokenId token) const;

    /// The first file of this name anywhere under `root`. Which token folder a
    /// file lands in depends on how the platform lays out its known folders,
    /// and that is not what these tests are checking.
    [[nodiscard]] static QString findRestored(const QString& root, const QString& name);
    [[nodiscard]] QString sourceHome() const { return workspace_.filePath("home"); }
    [[nodiscard]] QString archivePath(const QString& name) const {
        return workspace_.filePath(name);
    }

    QTemporaryDir workspace_;
    QString originalHome_;
    QHash<QString, QString> overriddenEnvironment_;
    std::unique_ptr<platform::PlatformService> platform_;
    bool distinguishesCase_ = true;

    /// How many files the documents profile should find. One fewer where the
    /// filesystem folded the case-only pair into a single file.
    quint64 documentFileCount_ = 8;
};

void ContinuityRoundTripTest::initTestCase() {
    QVERIFY(workspace_.isValid());

    // QStandardPaths follows HOME, so pointing it at the workspace gives the
    // services a self-contained machine to work with.
    //
    // The XDG variables have to go too. They override the defaults under HOME,
    // and a machine that sets them - a CI runner does - would otherwise have
    // the test read and capture its real configuration directory.
    originalHome_ = qEnvironmentVariable("HOME");
    for (const char* name : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME",
                             "XDG_DESKTOP_DIR", "XDG_DOCUMENTS_DIR", "XDG_DOWNLOAD_DIR",
                             "XDG_PICTURES_DIR", "XDG_MUSIC_DIR", "XDG_VIDEOS_DIR"}) {
        overriddenEnvironment_.insert(QString::fromLatin1(name), qEnvironmentVariable(name));
        qunsetenv(name);
    }
    qputenv("HOME", sourceHome().toUtf8());

    // Created after HOME is set, so its folder table describes the fake home.
    platform_ = platform::PlatformService::create();

    // Only if the platform actually reads HOME, though. Windows resolves its
    // known folders through the shell and has no notion of HOME at all, so
    // everything below would run against the account's real profile: writing
    // the fixture into their AppData, and capturing their documents. That is
    // not a test failing, it is a test doing something it has no business
    // doing, so it does not run there.
    for (const format::PathTokenId token :
         {format::PathTokenId::Documents, format::PathTokenId::AppConfig,
          format::PathTokenId::AppData}) {
        const QString base = baseFor(token);
        if (!base.startsWith(sourceHome())) {
            QSKIP(
                "this platform does not resolve its known folders from HOME, so the "
                "fixture would land in the real user profile");
        }
    }

    distinguishesCase_ = distinguishesCase(sourceHome());
    if (!distinguishesCase_) {
        qInfo("this filesystem folds case; the collision fixture is reduced to one file");
    }

    buildSourceTree(sourceHome());
}

void ContinuityRoundTripTest::cleanupTestCase() {
    if (!originalHome_.isEmpty()) {
        qputenv("HOME", originalHome_.toUtf8());
    }
    for (auto it = overriddenEnvironment_.constBegin(); it != overriddenEnvironment_.constEnd();
         ++it) {
        if (!it.value().isEmpty()) {
            qputenv(it.key().toLatin1().constData(), it.value().toUtf8());
        }
    }
}

bool ContinuityRoundTripTest::distinguishesCase(const QString& directory) {
    QDir().mkpath(directory);
    const QString lower = directory + QStringLiteral("/transmit-case-probe");
    const QString upper = directory + QStringLiteral("/TRANSMIT-CASE-PROBE");

    QFile probe(lower);
    if (!probe.open(QIODevice::WriteOnly)) {
        return true;  // cannot tell; assume the stricter of the two
    }
    probe.close();

    const bool folded = QFileInfo::exists(upper);
    QFile::remove(lower);
    return !folded;
}

QString ContinuityRoundTripTest::findRestored(const QString& root, const QString& name) {
    QDirIterator walker(root, {name}, QDir::Files, QDirIterator::Subdirectories);
    return walker.hasNext() ? walker.next() : QString();
}

QString ContinuityRoundTripTest::baseFor(format::PathTokenId token) const {
    const auto base = platform_->knownFolders().base(token);
    return base ? QString::fromStdString(*base) : QString();
}

void ContinuityRoundTripTest::buildSourceTree(const QString& home) {
    const auto write = [](const QString& path, const QByteArray& content) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
        file.write(content);
    };

    write(home + "/Documents/reports/q1.txt", "first quarter\n");
    write(home + "/Documents/reports/q2.txt", "second quarter\n");
    write(home + "/Documents/notes.txt", "lower case notes\n");

    // The twin only exists where the filesystem can hold both. Writing it
    // anyway on a case-insensitive volume would not create a second file, it
    // would silently overwrite the first - and every count below would then be
    // measuring something other than what it says.
    if (distinguishesCase_) {
        write(home + "/Documents/Notes.txt", "upper case notes\n");
    }
    documentFileCount_ = distinguishesCase_ ? 8 : 7;
    write(home + "/Documents/copy-a.bin", QByteArray(4096, 'd'));
    write(home + "/Documents/copy-b.bin", QByteArray(4096, 'd'));
    write(home + "/Pictures/holiday.jpg", QByteArray(20000, '\x7f'));

    // Incompressible content, so the split test really has to cross volumes
    // rather than fitting everything into one part after compression.
    QByteArray noise(30000, '\0');
    quint32 state = 0x1234567u;
    for (char& byte : noise) {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<char>((state >> 24) & 0xFFu);
    }
    write(home + "/Documents/random.bin", noise);

    // A program the catalog has never heard of. Its settings should still
    // travel: a migration that only carried the applications someone thought
    // to write a recipe for would be a poor one.
    // Written where this platform actually keeps them, not where Linux does:
    // ~/.config is not a known folder on macOS or Windows, so a file there
    // would not be captured at all and the test would be proving nothing.
    write(baseFor(format::PathTokenId::AppConfig) + "/some-obscure-tool/settings.json",
          "{\"root\":\"/tmp\"}\n");
    write(baseFor(format::PathTokenId::AppData) + "/some-obscure-tool/state.db",
          "not really a database\n");
    QDir().mkpath(home + "/Documents/empty");
}

core::CaptureSelection ContinuityRoundTripTest::documentsSelection() const {
    return core::ProfileService::profileById(QStringLiteral("documents")).selection;
}

void ContinuityRoundTripTest::capturesAndRestoresUserFiles() {
    core::ExportService exporter(*platform_);
    core::CancelToken token;

    core::ExportRequest request;
    request.destinationPath = archivePath("round-trip.txa");
    request.selection = documentsSelection();
    request.preset = format::CompressionPreset::Fast;

    const core::ExportReport exported = exporter.run(request, token);
    QVERIFY2(exported.succeeded, qPrintable(exported.errorMessage));
    QCOMPARE(exported.fileCount, documentFileCount_);
    QVERIFY(exported.storedBytes > 0);

    core::ImportService importer(*platform_);
    core::ImportRequest restore;
    restore.archivePath = request.destinationPath;
    restore.destinationOverride = workspace_.filePath("restored");

    const core::ImportReport imported = importer.run(restore, token);
    QVERIFY2(imported.succeeded, qPrintable(imported.errorMessage));
    QCOMPARE(imported.filesSkipped, 0u);

    // The bytes have to come back identical, not merely present.
    QFile original(sourceHome() + "/Documents/reports/q1.txt");
    QFile restored(workspace_.filePath("restored") + "/DOCUMENTS/reports/q1.txt");
    QVERIFY(original.open(QIODevice::ReadOnly));
    QVERIFY2(restored.open(QIODevice::ReadOnly), qPrintable(restored.fileName()));
    QCOMPARE(restored.readAll(), original.readAll());

    QVERIFY(QDir(workspace_.filePath("restored") + "/DOCUMENTS/empty").exists());
}

void ContinuityRoundTripTest::deduplicatesRepeatedContent() {
    core::ExportService exporter(*platform_);
    core::CancelToken token;

    core::ExportRequest request;
    request.destinationPath = archivePath("dedup.txa");
    request.selection = documentsSelection();
    request.preset = format::CompressionPreset::Fast;

    const core::ExportReport report = exporter.run(request, token);
    QVERIFY2(report.succeeded, qPrintable(report.errorMessage));

    // copy-a.bin and copy-b.bin hold the same 4 KiB, so one copy is free.
    QCOMPARE(report.deduplicatedBytes, 4096u);
}

void ContinuityRoundTripTest::splitsAcrossVolumesAndReadsThemBack() {
    core::ExportService exporter(*platform_);
    core::CancelToken token;

    core::ExportRequest request;
    request.destinationPath = archivePath("split.txa");
    request.selection = documentsSelection();
    request.preset = format::CompressionPreset::Fast;
    request.partSize = 8 * 1024;
    request.solidBlockSize = 4 * 1024;

    const core::ExportReport report = exporter.run(request, token);
    QVERIFY2(report.succeeded, qPrintable(report.errorMessage));
    QVERIFY2(report.archiveParts.size() > 1, "the payload should not fit in a single 8 KiB volume");

    core::ImportService importer(*platform_);
    core::ImportRequest restore;
    restore.archivePath = report.archiveParts.first();
    restore.destinationOverride = workspace_.filePath("restored-split");

    const core::ImportReport imported = importer.run(restore, token);
    QVERIFY2(imported.succeeded, qPrintable(imported.errorMessage));

    QFile original(sourceHome() + "/Pictures/holiday.jpg");
    QFile restored(workspace_.filePath("restored-split") + "/PICTURES/holiday.jpg");
    QVERIFY(original.open(QIODevice::ReadOnly));
    QVERIFY2(restored.open(QIODevice::ReadOnly), qPrintable(restored.fileName()));
    QCOMPARE(restored.readAll(), original.readAll());
}

void ContinuityRoundTripTest::encryptsWhenGivenAPassphrase() {
    if (!format::ArchiveCipher::isAvailable()) {
        QSKIP("this build has no OpenSSL");
    }

    core::ExportService exporter(*platform_);
    core::CancelToken token;

    core::ExportRequest request;
    request.destinationPath = archivePath("locked.txa");
    request.selection = documentsSelection();
    request.preset = format::CompressionPreset::Fast;
    request.passphrase = QStringLiteral("a passphrase worth remembering");

    const core::ExportReport report = exporter.run(request, token);
    QVERIFY2(report.succeeded, qPrintable(report.errorMessage));
    QVERIFY(report.encrypted);

    core::ImportService importer(*platform_);

    // Without the passphrase, even the file listing stays sealed.
    const core::ArchiveSummary locked = importer.inspect(request.destinationPath);
    QVERIFY(locked.valid);
    QVERIFY(locked.encrypted);
    QVERIFY(!locked.unlocked);
    QCOMPARE(locked.fileCount, 0u);

    const core::ArchiveSummary unlocked =
        importer.inspect(request.destinationPath, request.passphrase);
    QVERIFY(unlocked.unlocked);
    QCOMPARE(unlocked.fileCount, documentFileCount_);

    core::ImportRequest restore;
    restore.archivePath = request.destinationPath;
    restore.passphrase = QStringLiteral("the wrong one");
    restore.destinationOverride = workspace_.filePath("restored-locked");

    const core::ImportReport failed = importer.run(restore, token);
    QVERIFY(!failed.succeeded);
}

void ContinuityRoundTripTest::refusesCredentialsWithoutAPassphrase() {
    core::ExportService exporter(*platform_);
    core::CancelToken token;

    core::ExportRequest request;
    request.destinationPath = archivePath("secrets.txa");
    request.selection = documentsSelection();
    request.selection.domains.insert(static_cast<int>(format::DomainId::Secrets));

    // Saved passwords must never reach removable media unprotected.
    const core::ExportReport report = exporter.run(request, token);
    QVERIFY(!report.succeeded);
    QVERIFY(report.errorMessage.contains(QStringLiteral("encrypted")));
}

void ContinuityRoundTripTest::renamesFilesThatCollideOnACaseBlindTarget() {
    if (!distinguishesCase_) {
        QSKIP("this filesystem cannot hold the pair that is supposed to collide");
    }

    core::ExportService exporter(*platform_);
    core::CancelToken token;

    core::ExportRequest request;
    request.destinationPath = archivePath("cross-os.txa");
    request.selection = documentsSelection();
    request.preset = format::CompressionPreset::Fast;
    QVERIFY(exporter.run(request, token).succeeded);

    core::ImportService importer(*platform_);
    core::ImportRequest restore;
    restore.archivePath = request.destinationPath;
    restore.emulateOs = format::OsFamily::Windows;
    restore.dryRun = true;

    const core::ImportReport report = importer.run(restore, token);
    QVERIFY2(report.succeeded, qPrintable(report.errorMessage));

    // Notes.txt and notes.txt cannot coexist on Windows; one must be renamed
    // rather than silently overwriting the other.
    QCOMPARE(report.renames.size(), 1);

    QVERIFY(report.renames.first().second.contains(QStringLiteral("~1")));
}

void ContinuityRoundTripTest::reportsWhatARestoreWouldDoWithoutWriting() {
    core::ExportService exporter(*platform_);
    core::CancelToken token;

    core::ExportRequest request;
    request.destinationPath = archivePath("preview.txa");
    request.selection = documentsSelection();
    request.preset = format::CompressionPreset::Fast;
    QVERIFY(exporter.run(request, token).succeeded);

    const QString destination = workspace_.filePath("never-written");

    core::ImportService importer(*platform_);
    core::ImportRequest restore;
    restore.archivePath = request.destinationPath;
    restore.destinationOverride = destination;
    restore.dryRun = true;

    const core::ImportReport report = importer.run(restore, token);
    QVERIFY2(report.succeeded, qPrintable(report.errorMessage));
    QVERIFY(report.filesRestored > 0);
    QVERIFY2(!QDir(destination).exists(), "a dry run must not create anything");
}

void ContinuityRoundTripTest::honoursTheSkipConflictPolicy() {
    core::ExportService exporter(*platform_);
    core::CancelToken token;

    core::ExportRequest request;
    request.destinationPath = archivePath("conflict.txa");
    request.selection = documentsSelection();
    request.preset = format::CompressionPreset::Fast;
    QVERIFY(exporter.run(request, token).succeeded);

    const QString destination = workspace_.filePath("restored-conflict");
    core::ImportService importer(*platform_);

    core::ImportRequest first;
    first.archivePath = request.destinationPath;
    first.destinationOverride = destination;
    QVERIFY(importer.run(first, token).succeeded);

    // Everything is already in place, so a skipping restore should touch none
    // of it.
    core::ImportRequest second = first;
    second.conflictPolicy = core::ConflictPolicy::Skip;

    const core::ImportReport report = importer.run(second, token);
    QVERIFY2(report.succeeded, qPrintable(report.errorMessage));
    QCOMPARE(report.filesSkipped, documentFileCount_);
    QCOMPARE(report.bytesWritten, 0u);
}

/// An archive that stops half way cannot be restored from, and looks exactly
/// like a good one until somebody carries it to another machine and finds out.
void ContinuityRoundTripTest::aCancelledCaptureLeavesNoArchiveBehind() {
    core::ExportService exporter(*platform_);

    core::ExportRequest request;
    request.destinationPath = archivePath("cancelled.txa");
    request.selection = documentsSelection();
    request.preset = format::CompressionPreset::Fast;

    // Cancelled from the progress callback rather than up front, and only once
    // the run says it is transferring: by then the archive exists on disk and
    // has been written into. Cancelling any earlier would prove nothing, since
    // there would be no file to leave behind.
    core::CancelToken token;
    bool cancelledWhileWriting = false;
    const core::ExportReport report = exporter.run(
        request, token, [&token, &cancelledWhileWriting](const core::ProgressUpdate& update) {
            if (update.phase == core::ProgressPhase::Transferring) {
                cancelledWhileWriting = true;
                token.cancel();
            }
        });

    QVERIFY2(cancelledWhileWriting,
             "the capture never reported a transfer, so nothing was cancelled mid-write");
    QVERIFY2(!report.succeeded, "a cancelled capture must not report success");
    QVERIFY2(!QFileInfo::exists(request.destinationPath),
             qPrintable(QStringLiteral("a partly written archive was left at %1")
                            .arg(request.destinationPath)));
}

/// Naming a folder that is not there yet is how somebody says where they want
/// the archive; naming three that are not is a typo.
void ContinuityRoundTripTest::theArchiveFolderIsMadeButNotInvented() {
    core::ExportService exporter(*platform_);
    core::CancelToken token;

    core::ExportRequest request;
    request.selection = documentsSelection();
    request.preset = format::CompressionPreset::Fast;

    request.destinationPath = workspace_.filePath(QStringLiteral("backups/laptop.txa"));
    const core::ExportReport made = exporter.run(request, token);
    QVERIFY2(made.succeeded, qPrintable(made.errorMessage));
    QVERIFY(QFileInfo::exists(request.destinationPath));

    request.destinationPath = workspace_.filePath(QStringLiteral("nowhere/near/here/laptop.txa"));
    const core::ExportReport refused = exporter.run(request, token);
    QVERIFY2(!refused.succeeded, "a path of folders that do not exist should not be built out");
    QVERIFY2(refused.errorMessage.contains(QStringLiteral("nowhere")),
             qPrintable(QStringLiteral("the message should name the folder that is missing: %1")
                            .arg(refused.errorMessage)));
    QVERIFY(!QFileInfo::exists(workspace_.filePath(QStringLiteral("nowhere"))));
}

/// A restore writes into the places a person actually keeps things, so it has
/// to be reversible: what it replaced goes back, and what it added goes away.
void ContinuityRoundTripTest::aRestoreCanBeUndone() {
    core::ExportService exporter(*platform_);
    core::CancelToken token;

    core::ExportRequest request;
    request.destinationPath = archivePath("undo.txa");
    request.selection = documentsSelection();
    request.preset = format::CompressionPreset::Fast;
    QVERIFY(exporter.run(request, token).succeeded);

    const QString destination = workspace_.filePath("undo-target");

    // Deliberately not notes.txt: on a case-blind target that name is one half
    // of the collision pair, so which of the two lands there - and whether the
    // file already sitting there is the one replaced - depends on the
    // filesystem. q1.txt means the same thing everywhere.
    const QString existing = destination + "/DOCUMENTS/reports/q1.txt";
    QDir().mkpath(QFileInfo(existing).absolutePath());
    {
        QFile file(existing);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("what was here before\n");
    }

    core::ImportService importer(*platform_);
    core::ImportRequest restore;
    restore.archivePath = request.destinationPath;
    restore.destinationOverride = destination;
    restore.conflictPolicy = core::ConflictPolicy::Overwrite;
    restore.createRollback = true;

    const core::ImportReport report = importer.run(restore, token);
    QVERIFY2(report.succeeded, qPrintable(report.errorMessage));
    QVERIFY2(!report.rollbackArchivePath.isEmpty(), "an undo point should have been written");

    // The restore replaced one file and added several others.
    QFile replaced(existing);
    QVERIFY(replaced.open(QIODevice::ReadOnly));
    QCOMPARE(replaced.readAll(), QByteArray("first quarter\n"));
    replaced.close();
    QVERIFY(QFile::exists(destination + "/PICTURES/holiday.jpg"));

    const auto undone = core::RollbackWriter::undo(report.rollbackArchivePath);
    // QVERIFY2 evaluates its message eagerly, so the failure text has to be
    // built without touching error() on a Result that holds a value.
    const QString undoError = undone ? QString() : core::describeError(undone.error());
    QVERIFY2(undone, qPrintable(undoError));
    QVERIFY(undone->errors.isEmpty());
    QCOMPARE(undone->filesRestored, 1);
    QVERIFY(undone->filesRemoved > 0);

    // What was replaced is back, byte for byte.
    QFile putBack(existing);
    QVERIFY(putBack.open(QIODevice::ReadOnly));
    QCOMPARE(putBack.readAll(), QByteArray("what was here before\n"));

    // What was added is gone.
    QVERIFY2(!QFile::exists(destination + "/PICTURES/holiday.jpg"),
             "files the restore created should be removed again");
}

void ContinuityRoundTripTest::settingsOfAnUnknownProgramStillTravel() {
    core::ExportService exporter(*platform_);
    core::CancelToken token;

    core::ExportRequest request;
    request.destinationPath = archivePath("unknown-app.txa");
    request.selection = core::ProfileService::fullContinuity().selection;
    request.preset = format::CompressionPreset::Fast;

    const core::ExportReport exported = exporter.run(request, token);
    QVERIFY2(exported.succeeded, qPrintable(exported.errorMessage));

    const QString destination = workspace_.filePath("unknown-app-restored");
    core::ImportService importer(*platform_);
    core::ImportRequest restore;
    restore.archivePath = request.destinationPath;
    restore.destinationOverride = destination;
    restore.createRollback = false;

    QVERIFY(importer.run(restore, token).succeeded);

    // Found rather than looked up at a fixed path. macOS keeps configuration
    // and application data in the same directory, so both tokens resolve
    // there and whichever claims the folder first owns both files - which is
    // the right behaviour, and makes the exact token folder an implementation
    // detail. What the test is about is that the files travelled at all.
    const QString settingsPath = findRestored(destination, QStringLiteral("settings.json"));
    QVERIFY2(!settingsPath.isEmpty(), "the unknown program's settings did not travel");

    QFile settings(settingsPath);
    QVERIFY2(settings.open(QIODevice::ReadOnly), qPrintable(settingsPath));
    QCOMPARE(settings.readAll(), QByteArray("{\"root\":\"/tmp\"}\n"));

    QVERIFY2(!findRestored(destination, QStringLiteral("state.db")).isEmpty(),
             "the unknown program's data did not travel");
}

/// The catalog names an application's own directory, and the full profile also
/// takes the configuration tree that directory sits in. The same file must not
/// be read, compressed and stored twice.
void ContinuityRoundTripTest::overlappingRootsCaptureAFileOnlyOnce() {
    core::CaptureSelection selection;
    selection.domains = {static_cast<int>(format::DomainId::AppState)};

    core::CaptureRoot broad;
    broad.token = format::PathTokenId::AppConfig;
    broad.domain = format::DomainId::AppState;
    selection.roots.push_back(broad);

    core::CaptureRoot specific;
    specific.token = format::PathTokenId::AppConfig;
    specific.relative = QStringLiteral("some-obscure-tool");
    specific.domain = format::DomainId::AppState;
    specific.appId = QStringLiteral("test.tool");
    selection.roots.push_back(specific);

    const core::ScanService scanner(*platform_);
    core::CancelToken token;
    const core::ScanResult result = scanner.scan(selection, token);

    QStringList paths;
    for (const core::ScannedItem& item : result.items) {
        paths << item.absolutePath;
    }

    QSet<QString> unique(paths.begin(), paths.end());
    QCOMPARE(unique.size(), paths.size());

    // The counters have to agree with the list they describe.
    quint64 files = 0;
    for (const core::ScannedItem& item : result.items) {
        if (item.type == format::EntryType::File) {
            ++files;
        }
    }
    QCOMPARE(result.fileCount, files);
}

void ContinuityRoundTripTest::foldersAreRestoredBeforeWhatGoesInsideThem() {
    core::ExportService exporter(*platform_);
    core::CancelToken token;

    core::ExportRequest request;
    request.destinationPath = archivePath("order.txa");
    request.selection = documentsSelection();
    request.preset = format::CompressionPreset::Fast;

    const core::ExportReport exported = exporter.run(request, token);
    QVERIFY2(exported.succeeded, qPrintable(exported.errorMessage));

    core::ImportService importer(*platform_);
    core::ImportRequest restore;
    restore.archivePath = request.destinationPath;
    restore.destinationOverride = workspace_.filePath("restored-order");

    const core::ImportReport imported = importer.run(restore, token);
    QVERIFY2(imported.succeeded, qPrintable(imported.errorMessage));

    // The sort used to be on the raw enum, which runs File, Directory,
    // Symlink - so every file was written into a folder that did not exist
    // yet and only mkpath saved it. Nothing observable broke, which is why it
    // survived; this is the assertion that would have caught it.
    QSet<QString> foldersSeen;
    bool sawAFile = false;
    for (const core::RestoredItem& item : imported.items) {
        const QFileInfo info(item.targetPath);
        if (QDir(item.targetPath).exists() && !info.isSymLink()) {
            QVERIFY2(
                !sawAFile,
                qPrintable(QStringLiteral("folder %1 came after a file").arg(item.targetPath)));
            foldersSeen.insert(QDir::cleanPath(item.targetPath));
        } else {
            sawAFile = true;
            // And the folder it belongs in was one of them.
            const QString parent = QDir::cleanPath(info.absolutePath());
            QVERIFY2(foldersSeen.contains(parent) ||
                         !parent.startsWith(restore.destinationOverride + u'/'),
                     qPrintable(
                         QStringLiteral("%1 was written before its folder").arg(item.targetPath)));
        }
    }
    QVERIFY(!foldersSeen.isEmpty());
}

void ContinuityRoundTripTest::aFolderThatArrivesReadOnlyStillGetsItsContents() {
#ifdef Q_OS_WIN
    QSKIP("this is about POSIX modes, which Windows does not restore");
#else
    const QString locked = sourceHome() + QStringLiteral("/Documents/locked");
    QDir().mkpath(locked);
    QFile inside(locked + QStringLiteral("/kept.txt"));
    QVERIFY(inside.open(QIODevice::WriteOnly));
    inside.write("this has to come back\n");
    inside.close();

    // Readable and traversable, but not writable. The restore has to put the
    // file in before it applies that.
    QVERIFY(QFile::setPermissions(
        locked, QFile::ReadOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup));

    struct Restore {
        QString path;
        ~Restore() { QFile::setPermissions(path, QFile::permissions(path) | QFile::WriteOwner); }
    } const cleanup{locked};

    core::ExportService exporter(*platform_);
    core::CancelToken token;

    core::ExportRequest request;
    request.destinationPath = archivePath("readonly-folder.txa");
    request.selection = documentsSelection();
    request.preset = format::CompressionPreset::Fast;

    const core::ExportReport exported = exporter.run(request, token);
    QVERIFY2(exported.succeeded, qPrintable(exported.errorMessage));

    core::ImportService importer(*platform_);
    core::ImportRequest restore;
    restore.archivePath = request.destinationPath;
    restore.destinationOverride = workspace_.filePath("restored-readonly");

    const core::ImportReport imported = importer.run(restore, token);
    QVERIFY2(imported.succeeded, qPrintable(imported.errorMessage));
    QCOMPARE(imported.filesFailed, 0u);

    // Applying the mode as the folder was created left it unwritable with all
    // of its contents still to come, so every file inside failed - as the
    // account running the restore, at least; root is exempt from the check and
    // would not have noticed.
    const QString restoredFile =
        restore.destinationOverride + QStringLiteral("/DOCUMENTS/locked/kept.txt");
    QFile restored(restoredFile);
    QVERIFY2(restored.open(QIODevice::ReadOnly), qPrintable(restoredFile));
    QCOMPARE(restored.readAll(), QByteArray("this has to come back\n"));

    // And the mode still arrived.
    const QFile::Permissions mode =
        QFile::permissions(restore.destinationOverride + QStringLiteral("/DOCUMENTS/locked"));
    QVERIFY(!mode.testFlag(QFile::WriteOwner));

    QFile::setPermissions(restore.destinationOverride + QStringLiteral("/DOCUMENTS/locked"),
                          mode | QFile::WriteOwner);
    QFile::remove(inside.fileName());
    QDir().rmdir(locked);
#endif
}

QTEST_MAIN(ContinuityRoundTripTest)
#include "ContinuityRoundTripTest.moc"
