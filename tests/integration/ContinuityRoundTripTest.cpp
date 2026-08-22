#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "core/services/ExportService.h"
#include "core/services/ImportService.h"
#include "core/services/ProfileService.h"
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
    void cleanupTestCase();

private:
    /// Builds a home directory with the shapes that break naive tools: nested
    /// folders, duplicate content, an awkward name, and a case-only collision.
    void buildSourceTree(const QString& home);
    [[nodiscard]] core::CaptureSelection documentsSelection() const;
    [[nodiscard]] QString sourceHome() const { return workspace_.filePath("home"); }
    [[nodiscard]] QString archivePath(const QString& name) const {
        return workspace_.filePath(name);
    }

    QTemporaryDir workspace_;
    QString originalHome_;
    std::unique_ptr<platform::PlatformService> platform_;
};

void ContinuityRoundTripTest::initTestCase() {
    QVERIFY(workspace_.isValid());

    // QStandardPaths follows HOME, so pointing it at the workspace gives the
    // services a self-contained machine to work with.
    originalHome_ = qEnvironmentVariable("HOME");
    qputenv("HOME", sourceHome().toUtf8());

    buildSourceTree(sourceHome());
    platform_ = platform::PlatformService::create();
}

void ContinuityRoundTripTest::cleanupTestCase() {
    if (!originalHome_.isEmpty()) {
        qputenv("HOME", originalHome_.toUtf8());
    }
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
    write(home + "/Documents/Notes.txt", "upper case notes\n");
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
    QCOMPARE(exported.fileCount, 8u);
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
    QCOMPARE(unlocked.fileCount, 8u);

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
    QCOMPARE(report.filesSkipped, 8u);
    QCOMPARE(report.bytesWritten, 0u);
}

QTEST_MAIN(ContinuityRoundTripTest)
#include "ContinuityRoundTripTest.moc"
