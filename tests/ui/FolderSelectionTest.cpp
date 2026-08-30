// Choosing which of your own folders come with you.
//
// A profile is a reasonable default, not an answer to "my documents but not
// four hundred gigabytes of video". Until this list, the only choice was
// whether files of your own came at all.
//
// The two things worth checking are the two that would be quietly wrong: that
// unticking a folder actually removes it from what the capture takes, and that
// it removes only that - a profile's roots include application configuration
// and other things this list never offered, and swapping the whole list for
// the ticked folders would drop them without saying so.

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "app/ExportController.h"
#include "app/models/CaptureFolderModel.h"
#include "core/services/ProfileService.h"

using namespace transmit;
using transmit::app::CaptureFolderModel;
using transmit::app::ExportController;

class FolderSelectionTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void everyFolderIsListedWhetherOrNotThisMachineHasIt();
    void aFolderThisMachineHasNotGotCannotBeTicked();
    void theSizesAreMeasuredAndAddedUpPerFolder();
    void untickingAFolderTakesItOutOfTheCapture();
    void untickingAFolderLeavesEverythingElseTheProfileAskedFor();
    void anUntouchedListChangesNothing();

private:
    [[nodiscard]] int rowFor(const CaptureFolderModel& model, const QString& name) const;
    [[nodiscard]] static QVariant valueAt(const CaptureFolderModel& model, int row,
                                          CaptureFolderModel::Roles role) {
        return model.data(model.index(row), role);
    }

    QTemporaryDir home_;
    QString previousHome_;
    bool usable_ = false;
};

void FolderSelectionTest::initTestCase() {
    QVERIFY(home_.isValid());
    previousHome_ = qEnvironmentVariable("HOME");
    for (const char* name :
         {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_DOCUMENTS_DIR",
          "XDG_PICTURES_DIR", "XDG_VIDEOS_DIR", "XDG_MUSIC_DIR", "XDG_DOWNLOAD_DIR"}) {
        qunsetenv(name);
    }
    qputenv("HOME", home_.path().toUtf8());

    // Documents and Pictures exist and hold something of known size; Videos
    // deliberately does not exist, so the "not on this computer" case is real
    // rather than described.
    for (const QString& folder : {QStringLiteral("Documents"), QStringLiteral("Pictures")}) {
        QVERIFY(QDir().mkpath(home_.filePath(folder)));
    }
    QFile documents(home_.filePath(QStringLiteral("Documents/notes.txt")));
    QVERIFY(documents.open(QIODevice::WriteOnly));
    QCOMPARE(documents.write(QByteArray(4096, 'd')), 4096);
    documents.close();

    QFile pictures(home_.filePath(QStringLiteral("Pictures/photo.bin")));
    QVERIFY(pictures.open(QIODevice::WriteOnly));
    QCOMPARE(pictures.write(QByteArray(8192, 'p')), 8192);
    pictures.close();

    const CaptureFolderModel probe;
    usable_ =
        rowFor(probe, QStringLiteral("DOCUMENTS")) >= 0 &&
        valueAt(probe, rowFor(probe, QStringLiteral("DOCUMENTS")), CaptureFolderModel::PathRole)
            .toString()
            .startsWith(home_.path());
    if (!usable_) {
        QSKIP("this platform does not resolve its known folders from HOME");
    }
}

void FolderSelectionTest::cleanupTestCase() {
    if (!previousHome_.isEmpty()) {
        qputenv("HOME", previousHome_.toUtf8());
    }
}

int FolderSelectionTest::rowFor(const CaptureFolderModel& model, const QString& name) const {
    for (int row = 0; row < model.rowCount(); ++row) {
        if (valueAt(model, row, CaptureFolderModel::TokenNameRole).toString() == name) {
            return row;
        }
    }
    return -1;
}

void FolderSelectionTest::everyFolderIsListedWhetherOrNotThisMachineHasIt() {
    const CaptureFolderModel model;
    QVERIFY2(model.rowCount() >= 5, "the list of folders is suspiciously short");

    // Listed and turned off rather than hidden: the list reads the same on
    // every machine, and somebody comparing two of them can see what the
    // other one had.
    const int videos = rowFor(model, QStringLiteral("VIDEOS"));
    QVERIFY(videos >= 0);
    QCOMPARE(valueAt(model, videos, CaptureFolderModel::PresentRole).toBool(), false);
    QCOMPARE(valueAt(model, videos, CaptureFolderModel::SelectedRole).toBool(), false);

    const int documents = rowFor(model, QStringLiteral("DOCUMENTS"));
    QVERIFY(documents >= 0);
    QCOMPARE(valueAt(model, documents, CaptureFolderModel::PresentRole).toBool(), true);
    QCOMPARE(valueAt(model, documents, CaptureFolderModel::SelectedRole).toBool(), true);
}

void FolderSelectionTest::aFolderThisMachineHasNotGotCannotBeTicked() {
    CaptureFolderModel model;
    const int videos = rowFor(model, QStringLiteral("VIDEOS"));
    QVERIFY(videos >= 0);

    // Ticking it would promise something the capture then quietly does not do.
    model.setSelected(videos, true);
    QCOMPARE(valueAt(model, videos, CaptureFolderModel::SelectedRole).toBool(), false);

    model.selectAll();
    QCOMPARE(valueAt(model, videos, CaptureFolderModel::SelectedRole).toBool(), false);
}

void FolderSelectionTest::theSizesAreMeasuredAndAddedUpPerFolder() {
    CaptureFolderModel model;
    QSignalSpy measured(&model, &CaptureFolderModel::measuredChanged);
    model.measure();
    QVERIFY2(measured.wait(60000), "the folders were never measured");

    const int documents = rowFor(model, QStringLiteral("DOCUMENTS"));
    const int pictures = rowFor(model, QStringLiteral("PICTURES"));
    QVERIFY(documents >= 0 && pictures >= 0);

    // Each folder's own bytes, not the whole home directory's, or the numbers
    // beside them are the same number seven times and tell nobody anything.
    QCOMPARE(valueAt(model, documents, CaptureFolderModel::SizeBytesRole).toDouble(), 4096.0);
    QCOMPARE(valueAt(model, pictures, CaptureFolderModel::SizeBytesRole).toDouble(), 8192.0);
    QCOMPARE(valueAt(model, documents, CaptureFolderModel::FileCountRole).toDouble(), 1.0);

    QVERIFY2(valueAt(model, documents, CaptureFolderModel::SizeTextRole)
                 .toString()
                 .contains(QStringLiteral("4")),
             qPrintable(valueAt(model, documents, CaptureFolderModel::SizeTextRole).toString()));
}

void FolderSelectionTest::untickingAFolderTakesItOutOfTheCapture() {
    CaptureFolderModel model;
    const int pictures = rowFor(model, QStringLiteral("PICTURES"));
    QVERIFY(pictures >= 0);
    model.setSelected(pictures, false);
    QVERIFY(model.isNarrowed());

    ExportController controller;
    controller.chooseFolders(&model);

    const core::CaptureSelection selection =
        controller.selectionFor(QStringLiteral("full"), {}, false);

    const bool takesPictures = std::any_of(selection.roots.constBegin(), selection.roots.constEnd(),
                                           [](const core::CaptureRoot& root) {
                                               return root.token == format::PathTokenId::Pictures &&
                                                      root.domain == core::DomainId::UserData;
                                           });
    QVERIFY2(!takesPictures, "a folder that was unticked is still being captured");

    const bool takesDocuments =
        std::any_of(selection.roots.constBegin(), selection.roots.constEnd(),
                    [](const core::CaptureRoot& root) {
                        return root.token == format::PathTokenId::Documents &&
                               root.domain == core::DomainId::UserData;
                    });
    QVERIFY2(takesDocuments, "a folder that was left ticked was dropped as well");
}

// The list only offers folders of your own. A profile's roots include others -
// application configuration, for one - and replacing the whole list with the
// ticked folders would take those away without saying so, which is a capture
// missing things nobody chose to leave behind.
void FolderSelectionTest::untickingAFolderLeavesEverythingElseTheProfileAskedFor() {
    const core::CaptureSelection before =
        core::ProfileService::profileById(QStringLiteral("full")).selection;
    const auto otherRoots = [](const core::CaptureSelection& selection) {
        return std::count_if(selection.roots.constBegin(), selection.roots.constEnd(),
                             [](const core::CaptureRoot& root) {
                                 return root.domain != core::DomainId::UserData ||
                                        !root.appId.isEmpty();
                             });
    };
    QVERIFY2(otherRoots(before) > 0, "the profile has no other roots, so this proves nothing");

    CaptureFolderModel model;
    model.setSelected(rowFor(model, QStringLiteral("PICTURES")), false);

    ExportController controller;
    controller.chooseFolders(&model);
    const core::CaptureSelection after = controller.selectionFor(QStringLiteral("full"), {}, false);

    QCOMPARE(otherRoots(after), otherRoots(before));
}

void FolderSelectionTest::anUntouchedListChangesNothing() {
    CaptureFolderModel model;
    QVERIFY2(!model.isNarrowed(), "a list nobody has touched claims to have narrowed something");

    ExportController controller;
    const core::CaptureSelection untouched =
        controller.selectionFor(QStringLiteral("full"), {}, false);
    controller.chooseFolders(&model);
    const core::CaptureSelection chosen =
        controller.selectionFor(QStringLiteral("full"), {}, false);

    QCOMPARE(chosen.roots.size(), untouched.roots.size());
}

QTEST_MAIN(FolderSelectionTest)
#include "FolderSelectionTest.moc"
