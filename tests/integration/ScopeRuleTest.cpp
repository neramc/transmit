// What a folder is allowed to change about the rule the whole capture set.
//
// Every root carries a scope of its own, and almost none of them were given
// one: a root built for a user folder holds a default-constructed rule that
// nobody wrote. Merging that with the selection's is therefore not "the
// stricter of the two, field by field" - for anything whose default is already
// the strict answer, a rule nobody wrote is indistinguishable from a
// deliberate no, and taking the stricter of the two turned it off for every
// capture. `--follow-symlinks` did nothing at all on a user folder, and said
// nothing about it.
//
// So the rule is: a root may take less by size, date, type or pattern, and may
// not overrule a policy set for the whole capture.

#include <QDateTime>
#include <QTest>

#include "core/continuity/ContinuityTypes.h"

using namespace transmit;
using transmit::core::ScopeRule;

class ScopeRuleTest : public QObject {
    Q_OBJECT

private slots:
    void aFolderWithNoRuleOfItsOwnChangesNothing();
    void aFolderCanOnlyTightenASizeLimit();
    void aFolderCanOnlyNarrowTheDates();
    void theTypesAreIntersectedAndTheExclusionsAreAdded();
    void aFolderCanHideHiddenFilesButNotShowThem();
    void aPolicyForTheWholeCaptureIsNotOverruled();
    void narrowingIsNeverWidening();
};

// The case that matters most, because it is nearly every root there is.
void ScopeRuleTest::aFolderWithNoRuleOfItsOwnChangesNothing() {
    ScopeRule capture;
    capture.maximumFileSize = 1024;
    capture.minimumFileSize = 16;
    capture.includeExtensions = {QStringLiteral("txt")};
    capture.excludeExtensions = {QStringLiteral("iso")};
    capture.modifiedSince = QDateTime::fromSecsSinceEpoch(1000);
    capture.modifiedBefore = QDateTime::fromSecsSinceEpoch(9000);
    capture.excludePatterns = {QStringLiteral("**/node_modules/**")};
    capture.followSymlinks = true;
    capture.includeHidden = true;
    capture.fetchCloudFiles = true;

    const ScopeRule merged = capture.narrowedBy(ScopeRule{});

    QCOMPARE(merged.maximumFileSize, capture.maximumFileSize);
    QCOMPARE(merged.minimumFileSize, capture.minimumFileSize);
    QCOMPARE(merged.includeExtensions, capture.includeExtensions);
    QCOMPARE(merged.excludeExtensions, capture.excludeExtensions);
    QCOMPARE(merged.modifiedSince, capture.modifiedSince);
    QCOMPARE(merged.modifiedBefore, capture.modifiedBefore);
    QCOMPARE(merged.excludePatterns, capture.excludePatterns);
    QVERIFY2(merged.followSymlinks, "a folder that said nothing turned link-following off");
    QVERIFY(merged.includeHidden);
    QVERIFY2(merged.fetchCloudFiles, "a folder that said nothing turned the cloud files off");
}

void ScopeRuleTest::aFolderCanOnlyTightenASizeLimit() {
    ScopeRule capture;
    capture.maximumFileSize = 1024;

    ScopeRule smaller;
    smaller.maximumFileSize = 512;
    QCOMPARE(capture.narrowedBy(smaller).maximumFileSize, 512u);

    ScopeRule larger;
    larger.maximumFileSize = 4096;
    QCOMPARE(capture.narrowedBy(larger).maximumFileSize, 1024u);

    // Zero is "no limit", not "a limit of nothing", at both ends.
    QCOMPARE(capture.narrowedBy(ScopeRule{}).maximumFileSize, 1024u);
    QCOMPARE(ScopeRule{}.narrowedBy(smaller).maximumFileSize, 512u);
    QCOMPARE(ScopeRule{}.narrowedBy(ScopeRule{}).maximumFileSize, 0u);

    // The minimum runs the other way: the larger of the two takes less.
    ScopeRule floorOfSixteen;
    floorOfSixteen.minimumFileSize = 16;
    ScopeRule floorOfSixty;
    floorOfSixty.minimumFileSize = 64;
    QCOMPARE(floorOfSixteen.narrowedBy(floorOfSixty).minimumFileSize, 64u);
    QCOMPARE(floorOfSixty.narrowedBy(floorOfSixteen).minimumFileSize, 64u);
}

void ScopeRuleTest::aFolderCanOnlyNarrowTheDates() {
    const QDateTime early = QDateTime::fromSecsSinceEpoch(1000);
    const QDateTime late = QDateTime::fromSecsSinceEpoch(9000);

    ScopeRule capture;
    capture.modifiedSince = early;
    capture.modifiedBefore = late;

    ScopeRule tighter;
    tighter.modifiedSince = QDateTime::fromSecsSinceEpoch(2000);
    tighter.modifiedBefore = QDateTime::fromSecsSinceEpoch(8000);
    QCOMPARE(capture.narrowedBy(tighter).modifiedSince, tighter.modifiedSince);
    QCOMPARE(capture.narrowedBy(tighter).modifiedBefore, tighter.modifiedBefore);

    ScopeRule wider;
    wider.modifiedSince = QDateTime::fromSecsSinceEpoch(500);
    wider.modifiedBefore = QDateTime::fromSecsSinceEpoch(9500);
    QCOMPARE(capture.narrowedBy(wider).modifiedSince, early);
    QCOMPARE(capture.narrowedBy(wider).modifiedBefore, late);

    // A bound only one of them sets is still a bound.
    QCOMPARE(ScopeRule{}.narrowedBy(tighter).modifiedSince, tighter.modifiedSince);
    QVERIFY(!ScopeRule{}.narrowedBy(ScopeRule{}).modifiedSince.isValid());
}

void ScopeRuleTest::theTypesAreIntersectedAndTheExclusionsAreAdded() {
    ScopeRule capture;
    capture.includeExtensions = {QStringLiteral("txt"), QStringLiteral("md")};
    capture.excludeExtensions = {QStringLiteral("iso")};

    ScopeRule folder;
    folder.includeExtensions = {QStringLiteral("md"), QStringLiteral("pdf")};
    folder.excludeExtensions = {QStringLiteral("vmdk")};

    const ScopeRule merged = capture.narrowedBy(folder);

    // Only what both asked for: a folder cannot add pdf back.
    QCOMPARE(merged.includeExtensions, QSet<QString>{QStringLiteral("md")});
    QVERIFY(merged.excludeExtensions.contains(QStringLiteral("iso")));
    QVERIFY(merged.excludeExtensions.contains(QStringLiteral("vmdk")));

    // A folder that names types when the capture named none is narrowing.
    QCOMPARE(ScopeRule{}.narrowedBy(folder).includeExtensions, folder.includeExtensions);

    // And two lists with nothing in common leave a folder taking nothing,
    // which is what asking for two disjoint things means.
    ScopeRule elsewhere;
    elsewhere.includeExtensions = {QStringLiteral("png")};
    QVERIFY(capture.narrowedBy(elsewhere).includeExtensions.isEmpty());
}

void ScopeRuleTest::aFolderCanHideHiddenFilesButNotShowThem() {
    ScopeRule taking;      // includeHidden defaults to true
    ScopeRule leavingOut;  //
    leavingOut.includeHidden = false;

    QVERIFY(!taking.narrowedBy(leavingOut).includeHidden);
    QVERIFY(!leavingOut.narrowedBy(taking).includeHidden);
    QVERIFY(taking.narrowedBy(taking).includeHidden);
}

void ScopeRuleTest::aPolicyForTheWholeCaptureIsNotOverruled() {
    // The two whose defaults are already the strict answer. A folder cannot
    // turn them on and cannot turn them off; they belong to the capture.
    ScopeRule capture;
    capture.followSymlinks = true;
    capture.fetchCloudFiles = true;

    QVERIFY(capture.narrowedBy(ScopeRule{}).followSymlinks);
    QVERIFY(capture.narrowedBy(ScopeRule{}).fetchCloudFiles);

    ScopeRule keen;
    keen.followSymlinks = true;
    keen.fetchCloudFiles = true;
    QVERIFY2(!ScopeRule{}.narrowedBy(keen).followSymlinks,
             "a folder widened a policy the capture did not ask for");
    QVERIFY2(!ScopeRule{}.narrowedBy(keen).fetchCloudFiles,
             "a folder widened a policy the capture did not ask for");
}

// The property behind all of the above, said once: whatever a folder carries,
// the merged rule never accepts a file the capture's own rule would refuse.
void ScopeRuleTest::narrowingIsNeverWidening() {
    ScopeRule capture;
    capture.maximumFileSize = 1000;
    capture.minimumFileSize = 10;
    capture.excludeExtensions = {QStringLiteral("iso")};
    capture.includeHidden = false;

    const ScopeRule folders[] = {
        ScopeRule{},
        [] {
            ScopeRule r;
            r.maximumFileSize = 100000;
            r.minimumFileSize = 0;
            return r;
        }(),
        [] {
            ScopeRule r;
            r.includeHidden = true;
            r.excludeExtensions = {QStringLiteral("png")};
            return r;
        }(),
    };

    for (const ScopeRule& folder : folders) {
        const ScopeRule merged = capture.narrowedBy(folder);
        QVERIFY(merged.maximumFileSize != 0 && merged.maximumFileSize <= capture.maximumFileSize);
        QVERIFY(merged.minimumFileSize >= capture.minimumFileSize);
        QVERIFY(merged.excludeExtensions.contains(QStringLiteral("iso")));
        QVERIFY(!merged.includeHidden);
    }
}

QTEST_MAIN(ScopeRuleTest)
#include "ScopeRuleTest.moc"
