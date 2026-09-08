#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "core/rewrite/RewritePlan.h"

namespace transmit::core {

/// Staging, applying and undoing a change inside somebody's settings file.
///
/// This is the most invasive thing Transmit does - it edits a file the user
/// already had - so the whole pass keeps a copy of every original and can be
/// put back. Every case here is about one of the two ways that goes wrong: the
/// file ends up neither the old one nor the new one, or the copy that would
/// have saved it is somewhere nobody is told about.
class RewritePlanTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void anEditThatChangesNothingIsNotAnEdit();
    void countsEachFileOnceHoweverManyEditsItHas();
    void putsTheStagedFileInPlaceAndKeepsTheOriginal();
    void leavesAFileAloneWhenNothingWasStagedForIt();
    void createsAFileThatWasNotThereBefore();
    void saysSoWhenTheOriginalCannotBeSetAside();
    void putsTheOriginalBackAndTakesTheCopyAway();
    void revertingWithNothingKeptDoesNothing();
    void saysWhereTheFileIsWhenItCannotBePutBack();
    void throwsAwayTheKeptOriginalsOnlyWhenAsked();
    void everyEditBecomesSomethingTheUserCanRead();

private:
    [[nodiscard]] QString path(const QString& name) const { return workspace_.filePath(name); }

    void write(const QString& name, const QByteArray& bytes) const {
        QFile file(path(name));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(bytes);
    }

    [[nodiscard]] QByteArray read(const QString& name) const {
        QFile file(path(name));
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return file.readAll();
    }

    [[nodiscard]] RewriteEdit edit(const QString& name,
                                   const QString& where = QStringLiteral("a "
                                                                         "key"),
                                   const QString& from = QStringLiteral("/home/old"),
                                   const QString& to = QStringLiteral("/home/new")) const {
        return RewriteEdit{path(name), where, from, to, QStringLiteral("org.example.app")};
    }

    QTemporaryDir workspace_;
};

void RewritePlanTest::init() {
    QVERIFY(workspace_.isValid());
}

void RewritePlanTest::cleanup() {
    // Each case starts from an empty folder, so a leftover backup from one
    // cannot make the next one pass.
    QDir folder(workspace_.path());
    for (const QString& name : folder.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFileInfo entry(folder.filePath(name));
        if (entry.isDir()) {
            QDir(entry.absoluteFilePath()).removeRecursively();
        } else {
            QFile::remove(entry.absoluteFilePath());
        }
    }
}

void RewritePlanTest::anEditThatChangesNothingIsNotAnEdit() {
    RewritePlan plan;
    plan.add(edit(QStringLiteral("a.conf"), QStringLiteral("key"), QStringLiteral("same"),
                  QStringLiteral("same")));
    QVERIFY2(plan.isEmpty(), "an edit from a value to itself would be a backup for nothing");
    QCOMPARE(plan.fileCount(), 0);
}

void RewritePlanTest::countsEachFileOnceHoweverManyEditsItHas() {
    RewritePlan plan;
    plan.add(edit(QStringLiteral("a.conf"), QStringLiteral("first")));
    plan.add(edit(QStringLiteral("a.conf"), QStringLiteral("second")));
    plan.add(edit(QStringLiteral("b.conf"), QStringLiteral("third")));

    QCOMPARE(plan.edits().size(), 3);
    QCOMPARE(plan.fileCount(), 2);
    QCOMPARE(plan.files().size(), 2);
    // In the order they were first mentioned, so a report reads the way the
    // plan was built.
    QCOMPARE(plan.files().first(), path(QStringLiteral("a.conf")));
}

void RewritePlanTest::putsTheStagedFileInPlaceAndKeepsTheOriginal() {
    write(QStringLiteral("a.conf"), "home=/home/old\n");
    write(QStringLiteral("a.conf.transmit-staged"), "home=/home/new\n");

    RewritePlan plan;
    plan.add(edit(QStringLiteral("a.conf")));

    QStringList errors;
    QCOMPARE(plan.apply(&errors), 1);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));

    QCOMPARE(read(QStringLiteral("a.conf")), QByteArray("home=/home/new\n"));
    QCOMPARE(read(QStringLiteral("a.conf.transmit-backup")), QByteArray("home=/home/old\n"));
    QVERIFY2(!QFile::exists(path(QStringLiteral("a.conf.transmit-staged"))),
             "the staged file is litter once it has been put in place");
}

void RewritePlanTest::leavesAFileAloneWhenNothingWasStagedForIt() {
    // A rewriter that found nothing to change produces no staged file, and the
    // plan must not then invent a backup of an untouched file.
    write(QStringLiteral("a.conf"), "home=/home/old\n");

    RewritePlan plan;
    plan.add(edit(QStringLiteral("a.conf")));

    QStringList errors;
    QCOMPARE(plan.apply(&errors), 0);
    QVERIFY(errors.isEmpty());
    QCOMPARE(read(QStringLiteral("a.conf")), QByteArray("home=/home/old\n"));
    QVERIFY(!QFile::exists(path(QStringLiteral("a.conf.transmit-backup"))));
}

void RewritePlanTest::createsAFileThatWasNotThereBefore() {
    // A rewriter may produce a file the machine did not have. There is no
    // original, so there is nothing to keep, and that is not an error.
    write(QStringLiteral("new.conf.transmit-staged"), "home=/home/new\n");

    RewritePlan plan;
    plan.add(edit(QStringLiteral("new.conf")));

    QStringList errors;
    QCOMPARE(plan.apply(&errors), 1);
    QVERIFY(errors.isEmpty());
    QCOMPARE(read(QStringLiteral("new.conf")), QByteArray("home=/home/new\n"));
    QVERIFY(!QFile::exists(path(QStringLiteral("new.conf.transmit-backup"))));
}

void RewritePlanTest::saysSoWhenTheOriginalCannotBeSetAside() {
    // A folder where a file is expected: it exists, so there is something to
    // set aside, and it cannot be copied. Nothing may be changed on the way
    // out, and the person has to be told which file it was.
    QVERIFY(QDir().mkpath(path(QStringLiteral("stubborn.conf"))));
    write(QStringLiteral("stubborn.conf.transmit-staged"), "home=/home/new\n");

    RewritePlan plan;
    plan.add(edit(QStringLiteral("stubborn.conf")));

    QStringList errors;
    QCOMPARE(plan.apply(&errors), 0);
    QCOMPARE(errors.size(), 1);
    QVERIFY2(errors.first().contains(QStringLiteral("stubborn.conf")), qPrintable(errors.first()));
    QVERIFY2(QDir(path(QStringLiteral("stubborn.conf"))).exists(),
             "what could not be copied must still be where it was");
    QVERIFY2(!QFile::exists(path(QStringLiteral("stubborn.conf.transmit-staged"))),
             "the staged file is cleared away when it will not be used");
}

void RewritePlanTest::putsTheOriginalBackAndTakesTheCopyAway() {
    write(QStringLiteral("a.conf"), "home=/home/old\n");
    write(QStringLiteral("a.conf.transmit-staged"), "home=/home/new\n");

    RewritePlan plan;
    plan.add(edit(QStringLiteral("a.conf")));
    QCOMPARE(plan.apply(), 1);

    QStringList errors;
    QCOMPARE(plan.revert(&errors), 1);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));

    QCOMPARE(read(QStringLiteral("a.conf")), QByteArray("home=/home/old\n"));
    QVERIFY2(!QFile::exists(path(QStringLiteral("a.conf.transmit-backup"))),
             "a backup left behind after an undo is litter in somebody's config folder");
}

void RewritePlanTest::revertingWithNothingKeptDoesNothing() {
    write(QStringLiteral("a.conf"), "home=/home/old\n");

    RewritePlan plan;
    plan.add(edit(QStringLiteral("a.conf")));

    QStringList errors;
    QCOMPARE(plan.revert(&errors), 0);
    QVERIFY2(errors.isEmpty(), "there is nothing to put back and nothing went wrong");
    QCOMPARE(read(QStringLiteral("a.conf")), QByteArray("home=/home/old\n"));
}

void RewritePlanTest::saysWhereTheFileIsWhenItCannotBePutBack() {
    // The undo has to remove what is in the way before it can put the original
    // back, and a folder with something in it will not be removed. So the
    // original stays under a name the person has never heard of, and the one
    // thing the message must do is say that name.
    const QString target = path(QStringLiteral("occupied.conf"));
    QVERIFY(QDir().mkpath(target));
    write(QStringLiteral("occupied.conf/something-inside"), "in the way\n");
    write(QStringLiteral("occupied.conf.transmit-backup"), "home=/home/old\n");

    RewritePlan plan;
    plan.add(edit(QStringLiteral("occupied.conf")));

    QStringList errors;
    QCOMPARE(plan.revert(&errors), 0);
    QCOMPARE(errors.size(), 1);
    QVERIFY2(errors.first().contains(QStringLiteral("occupied.conf.transmit-backup")),
             qPrintable(errors.first()));
    QCOMPARE(read(QStringLiteral("occupied.conf.transmit-backup")), QByteArray("home=/home/old\n"));
}

void RewritePlanTest::throwsAwayTheKeptOriginalsOnlyWhenAsked() {
    write(QStringLiteral("a.conf"), "one\n");
    write(QStringLiteral("a.conf.transmit-staged"), "two\n");
    write(QStringLiteral("b.conf"), "three\n");
    write(QStringLiteral("b.conf.transmit-staged"), "four\n");

    RewritePlan plan;
    plan.add(edit(QStringLiteral("a.conf")));
    plan.add(edit(QStringLiteral("b.conf")));
    QCOMPARE(plan.apply(), 2);

    // Until this is called the backups are what makes the restore reversible.
    QVERIFY(QFile::exists(path(QStringLiteral("a.conf.transmit-backup"))));
    QCOMPARE(RewritePlan::discardBackups(plan.files()), 2);
    QVERIFY(!QFile::exists(path(QStringLiteral("a.conf.transmit-backup"))));
    QVERIFY(!QFile::exists(path(QStringLiteral("b.conf.transmit-backup"))));

    // The files themselves are untouched by throwing the copies away.
    QCOMPARE(read(QStringLiteral("a.conf")), QByteArray("two\n"));

    // And asking twice is not an error, it is just nothing left to remove.
    QCOMPARE(RewritePlan::discardBackups(plan.files()), 0);
}

void RewritePlanTest::everyEditBecomesSomethingTheUserCanRead() {
    RewritePlan plan;
    plan.add(edit(QStringLiteral("profiles.ini"), QStringLiteral("Path"),
                  QStringLiteral("/home/olduser/.mozilla"), QStringLiteral("/home/me/.mozilla")));

    const QList<ContinuityNote> notes = plan.toNotes();
    QCOMPARE(notes.size(), 1);
    QCOMPARE(notes.first().grade, ContinuityGrade::Adapted);
    QCOMPARE(notes.first().domain, DomainId::AppState);

    // The file by the name the person knows it by, not the whole path, and
    // both values so they can see what changed into what.
    QCOMPARE(notes.first().subject, QStringLiteral("profiles.ini - Path"));
    QVERIFY(notes.first().detail.contains(QStringLiteral("/home/me/.mozilla")));
    QVERIFY(notes.first().detail.contains(QStringLiteral("/home/olduser/.mozilla")));
}

}  // namespace transmit::core

QTEST_GUILESS_MAIN(transmit::core::RewritePlanTest)
#include "RewritePlanTest.moc"
