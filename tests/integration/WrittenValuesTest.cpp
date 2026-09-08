#include <QDateTime>
#include <QTest>

#include "core/utils/WrittenValues.h"

namespace transmit::core {

/// The values somebody types rather than picks.
///
/// Every one of these used to be inside the command line program where nothing
/// could ask it anything, and two of them answered wrongly there: a size large
/// enough to wrap came back as a limit nobody asked for, and a number of weeks
/// large enough to overflow an int was undefined behaviour. Both are cases
/// here now.
class WrittenValuesTest : public QObject {
    Q_OBJECT

private slots:
    void readsTheSizesPeopleWrite();
    void refusesWhatIsNotASize();
    void refusesASizeTooLargeToHold();
    void readsTheDatesPeopleWrite();
    void refusesWhatIsNotADate();
    void refusesADateOffTheEndOfTheCalendar();
    void readsAListOfFileTypes();

private:
    /// A fixed point to measure the relative forms against, so these cases say
    /// the same thing tomorrow as today.
    ///
    /// Built by parsing rather than by handing a Qt::TimeSpec to a QDateTime
    /// constructor: that overload is deprecated from Qt 6.9, its replacement
    /// wants a QTimeZone and only exists from 6.5, and this has to build
    /// against both. Parsing an ISO string with a Z on the end says the same
    /// thing to every version of Qt there is.
    [[nodiscard]] static QDateTime atUtc(const char* isoWithZone) {
        const QDateTime when = QDateTime::fromString(QString::fromLatin1(isoWithZone), Qt::ISODate);
        Q_ASSERT(when.isValid());
        return when;
    }

    [[nodiscard]] static QDateTime fixedNow() { return atUtc("2026-09-08T12:00:00Z"); }
};

void WrittenValuesTest::readsTheSizesPeopleWrite() {
    QCOMPARE(sizeFromText(QStringLiteral("512")).value_or(0), 512u);
    QCOMPARE(sizeFromText(QStringLiteral("64K")).value_or(0), 65536u);
    QCOMPARE(sizeFromText(QStringLiteral("3584M")).value_or(0), 3758096384u);
    QCOMPARE(sizeFromText(QStringLiteral("2G")).value_or(0), 2147483648u);
    QCOMPARE(sizeFromText(QStringLiteral("8T")).value_or(0), 8796093022208u);

    // Case and spacing are how somebody typed it, not what they meant.
    QCOMPARE(sizeFromText(QStringLiteral("2g")).value_or(0), 2147483648u);
    QCOMPARE(sizeFromText(QStringLiteral("  64 K  ")).value_or(0), 65536u);

    // Zero is a number. What it means is the caller's business - in a scope
    // rule it is "no limit" - but it is not a refusal.
    QCOMPARE(sizeFromText(QStringLiteral("0")).value_or(1), 0u);
}

void WrittenValuesTest::refusesWhatIsNotASize() {
    for (const QString& written :
         {QStringLiteral(""), QStringLiteral("   "), QStringLiteral("1.5G"), QStringLiteral("64KB"),
          QStringLiteral("-5"), QStringLiteral("0x10"), QStringLiteral("big"),
          QStringLiteral("K")}) {
        QVERIFY2(!sizeFromText(written).has_value(), qPrintable(written));
    }
}

void WrittenValuesTest::refusesASizeTooLargeToHold() {
    // The whole number fits and the multiplication does not. This used to wrap
    // round silently and come back as a limit of about fourteen exabytes,
    // which is not what was asked for and does not look wrong anywhere after.
    QVERIFY(!sizeFromText(QStringLiteral("18446744073709551G")).has_value());
    QVERIFY(!sizeFromText(QStringLiteral("17179869184G")).has_value());

    // The largest that does fit still comes back.
    QCOMPARE(sizeFromText(QStringLiteral("17179869183G")).value_or(0),
             17179869183ULL * 1024 * 1024 * 1024);
}

void WrittenValuesTest::readsTheDatesPeopleWrite() {
    const QDateTime now = fixedNow();

    QCOMPARE(timeFromText(QStringLiteral("30d"), now), now.addDays(-30));
    QCOMPARE(timeFromText(QStringLiteral("2w"), now), now.addDays(-14));
    QCOMPARE(timeFromText(QStringLiteral("6m"), now), now.addMonths(-6));
    QCOMPARE(timeFromText(QStringLiteral("2y"), now), now.addYears(-2));

    // Nothing back is now, which is a boundary rather than a mistake.
    QCOMPARE(timeFromText(QStringLiteral("0d"), now), now);

    // And the absolute forms, for a script that wants a fixed boundary.
    QCOMPARE(timeFromText(QStringLiteral("2024-01-15"), now),
             QDateTime(QDate(2024, 1, 15), QTime(0, 0, 0)));
    QCOMPARE(timeFromText(QStringLiteral("2024-05-06T12:00:00Z"), now),
             atUtc("2024-05-06T12:00:00Z"));
}

void WrittenValuesTest::refusesWhatIsNotADate() {
    const QDateTime now = fixedNow();
    for (const QString& written :
         {QStringLiteral(""), QStringLiteral("   "), QStringLiteral("-5d"), QStringLiteral("d"),
          QStringLiteral("not-a-date"), QStringLiteral("2024-13-45"), QStringLiteral("30")}) {
        QVERIFY2(!timeFromText(written, now).has_value(), qPrintable(written));
    }
}

void WrittenValuesTest::refusesADateOffTheEndOfTheCalendar() {
    const QDateTime now = fixedNow();

    // Four hundred million weeks used to be multiplied by seven inside an int,
    // which is undefined behaviour: the sanitisers say so, and a build without
    // them produces whatever the overflow left behind.
    QVERIFY(!timeFromText(QStringLiteral("400000000w"), now).has_value());

    // Two billion years used to come back as the first of January 1970 - a
    // date, before now, and nothing to do with what was asked for. A wrong
    // answer that looks like a right one is worse than no answer.
    QVERIFY(!timeFromText(QStringLiteral("2000000000y"), now).has_value());
    QVERIFY(!timeFromText(QStringLiteral("99999999999999999999d"), now).has_value());

    // Ten thousand years back is the boundary, and either side of it behaves.
    QVERIFY(timeFromText(QStringLiteral("3999999d"), now).has_value());
    QVERIFY(!timeFromText(QStringLiteral("4000001d"), now).has_value());
}

void WrittenValuesTest::readsAListOfFileTypes() {
    const QSet<QString> wanted = extensionsFromText(QStringLiteral(".TXT, md ,,.tar.gz,"));
    QCOMPARE(wanted.size(), 3);
    QVERIFY(wanted.contains(QStringLiteral("txt")));
    QVERIFY(wanted.contains(QStringLiteral("md")));
    QVERIFY(wanted.contains(QStringLiteral("tar.gz")));

    // A dot on its own names no file type, and neither does nothing at all.
    QVERIFY(extensionsFromText(QStringLiteral("...")).isEmpty());
    QVERIFY(extensionsFromText(QStringLiteral(" , , ")).isEmpty());
    QVERIFY(extensionsFromText(QString()).isEmpty());
}

}  // namespace transmit::core

QTEST_GUILESS_MAIN(transmit::core::WrittenValuesTest)
#include "WrittenValuesTest.moc"
