// Where the time went.
//
// Every optimisation in this codebase is expected to arrive with the two
// numbers it moved, and this is what produces them. A timer that quietly
// dropped a stage, or double-counted one, would make those numbers worse than
// having none - so the arithmetic is held to here rather than trusted.

#include <QFileInfo>
#include <QTest>

#include <thread>

#include "core/utils/Conversions.h"
#include "core/utils/StageTimer.h"

using namespace transmit;

class StageTimerTest : public QObject {
    Q_OBJECT

private slots:
    void aStageIsMeasuredAndNamed();
    void theSameStageTwiceIsSummedAndCounted();
    void beginningANewStageEndsThePreviousOne();
    void aScopeEndsItsStageOnItsOwn();
    void addRecordsWorkThatHappensInsideAnotherStage();
    void theSummaryPutsTheSlowestFirst();
    void endingWithNothingRunningIsHarmless();
    void clearForgetsEverything();

    void theFastExtensionAgreesWithQt();
    void theFastExtensionAgreesWithQt_data();

private:
    /// Long enough that a coarse clock still sees it, short enough that the
    /// suite stays fast.
    static void spend(int milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
};

void StageTimerTest::aStageIsMeasuredAndNamed() {
    core::StageTimer timer;
    timer.begin(QStringLiteral("scan"));
    spend(12);
    timer.end();

    const auto stages = timer.stages();
    QCOMPARE(stages.size(), 1);
    QCOMPARE(stages.constFirst().name, QStringLiteral("scan"));
    QCOMPARE(stages.constFirst().count, 1ULL);
    QVERIFY2(stages.constFirst().nanoseconds > 0, "a stage that took time was recorded as zero");
    QVERIFY(stages.constFirst().milliseconds() >= 1.0);
}

// A stage inside a loop - reading a file, hashing it - runs thousands of
// times. Listing it thousands of times would not be a report.
void StageTimerTest::theSameStageTwiceIsSummedAndCounted() {
    core::StageTimer timer;
    for (int i = 0; i < 3; ++i) {
        timer.begin(QStringLiteral("read"));
        spend(4);
        timer.end();
    }

    QCOMPARE(timer.stages().size(), 1);
    QCOMPARE(timer.stages().constFirst().count, 3ULL);
    QVERIFY(timer.stages().constFirst().milliseconds() >= 3.0);
}

void StageTimerTest::beginningANewStageEndsThePreviousOne() {
    core::StageTimer timer;
    timer.begin(QStringLiteral("first"));
    spend(8);
    timer.begin(QStringLiteral("second"));
    spend(8);
    timer.end();

    QCOMPARE(timer.stages().size(), 2);
    for (const core::StageTiming& stage : timer.stages()) {
        QVERIFY2(stage.nanoseconds > 0, qPrintable(stage.name));
        QCOMPARE(stage.count, 1ULL);
    }
}

void StageTimerTest::aScopeEndsItsStageOnItsOwn() {
    core::StageTimer timer;
    {
        const core::StageTimer::Scope scope(timer, QStringLiteral("pack"));
        spend(6);
    }
    // Nothing is running, so this must not add a second measurement.
    timer.end();

    QCOMPARE(timer.stages().size(), 1);
    QCOMPARE(timer.stages().constFirst().count, 1ULL);
}

void StageTimerTest::addRecordsWorkThatHappensInsideAnotherStage() {
    core::StageTimer timer;
    timer.begin(QStringLiteral("read"));
    timer.add(QStringLiteral("hash"), 5'000'000);
    spend(4);
    timer.end();

    QCOMPARE(timer.stages().size(), 2);

    bool sawHash = false;
    for (const core::StageTiming& stage : timer.stages()) {
        if (stage.name == QStringLiteral("hash")) {
            sawHash = true;
            QCOMPARE(stage.nanoseconds, 5'000'000);
            QCOMPARE(stage.count, 1ULL);
        }
    }
    QVERIFY(sawHash);
}

// The point of reading this is to find out what to look at, and the order the
// stages happened to run in does not answer that.
void StageTimerTest::theSummaryPutsTheSlowestFirst() {
    core::StageTimer timer;
    timer.add(QStringLiteral("quick"), 1'000'000);
    timer.add(QStringLiteral("slow"), 900'000'000);
    timer.add(QStringLiteral("middling"), 40'000'000);

    const QString summary = timer.summary();
    const int slow = static_cast<int>(summary.indexOf(QStringLiteral("slow")));
    const int middling = static_cast<int>(summary.indexOf(QStringLiteral("middling")));
    const int quick = static_cast<int>(summary.indexOf(QStringLiteral("quick")));

    QVERIFY2(slow >= 0 && middling >= 0 && quick >= 0, qPrintable(summary));
    QVERIFY2(slow < middling && middling < quick, qPrintable(summary));
}

void StageTimerTest::endingWithNothingRunningIsHarmless() {
    core::StageTimer timer;
    timer.end();
    timer.end();
    QVERIFY(timer.stages().isEmpty());
    QCOMPARE(timer.measuredNanoseconds(), 0);
    QVERIFY(timer.summary().isEmpty());
}

void StageTimerTest::clearForgetsEverything() {
    core::StageTimer timer;
    timer.add(QStringLiteral("something"), 1'000'000);
    timer.begin(QStringLiteral("running"));
    timer.clear();

    QVERIFY(timer.stages().isEmpty());
    QCOMPARE(timer.measuredNanoseconds(), 0);

    // The stage that was running when clear() was called must not come back
    // the next time anything ends.
    timer.end();
    QVERIFY(timer.stages().isEmpty());
}

// Not a timer, but it belongs to the same change: the capture sorts by
// extension, and it used to build two QFileInfo objects for every one of the
// n log n comparisons. The replacement is only worth having if it gives the
// same answer, including on the names that are easy to get wrong.
void StageTimerTest::theFastExtensionAgreesWithQt_data() {
    QTest::addColumn<QString>("path");

    for (const char* path :
         {"/home/bob/notes.txt", "/home/bob/Archive.TAR.GZ", "/home/bob/.bashrc",
          "/home/bob/.config/app.json", "/home/bob/noextension", "/home/bob/trailing.",
          "/home/bob/dir.with.dot/file", "/home/bob/dir.with.dot/file.md", "relative.png",
          ".hidden", "no-slash-no-dot", "/ends/with/slash/", "a.b.c.d.e", "/home/bob/UPPER.PNG",
          "/home/bob/spaced name.tar.bz2", "", "C:/Users/bob/Documents/report.DOCX",
          "C:\\Users\\bob\\.vimrc"}) {
        QTest::newRow(path[0] == '\0' ? "(empty)" : path) << QString::fromUtf8(path);
    }
}

void StageTimerTest::theFastExtensionAgreesWithQt() {
    QFETCH(QString, path);

    // QFileInfo is the definition being matched, so it is what this compares
    // against rather than a second hand-written list of expected answers.
    const QString expected = QFileInfo(path).suffix().toLower();
    QCOMPARE(core::fileExtension(path), expected);
}

QTEST_MAIN(StageTimerTest)
#include "StageTimerTest.moc"
