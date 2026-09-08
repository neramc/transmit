#include <QTest>

#include "platform/linux/LinuxSettingsProvider.h"

namespace transmit::platform {

/// What a desktop's answer means.
///
/// Every rule here is a rule about text, and every one of them used to live
/// inside the function that shells out to gsettings - so none could be asked
/// anything without a machine running that desktop, in that configuration,
/// with those settings already chosen. Which is the same as saying none of
/// them was ever checked.
///
/// Two were wrong. Both are the kind that does not fail: the setting is
/// recorded, the capture succeeds, and what arrives on the new machine is
/// nonsense.
class LinuxSettingsTest : public QObject {
    Q_OBJECT

private slots:
    void readsTheLayoutsGnomeActuallyLists();
    void anEmptyInputSourceListIsNotALayout();
    void ignoresInputMethodsBecauseTheyCannotBeWrittenBack();
    void readsADurationWhicheverWayItIsPrinted();
    void refusesADurationThatIsNotOne();
    void saysWhichThemeADesktopIsUsing();
    void keepsALocaleModifierWhereItBelongs();
    void aLocaleSurvivesTheRoundTrip();
    void readsTheZoneOutOfTheLink();
    void stripsTheQuotesGsettingsAdds();
};

/// Two layouts are two layouts.
///
/// This used to delete every bracket, quote and the words "xkb" and "ibus"
/// from the value and split what was left on commas, which left an empty
/// element between each pair - so a machine with "us" and "kr" recorded three
/// layouts, the middle one nothing at all.
void LinuxSettingsTest::readsTheLayoutsGnomeActuallyLists() {
    QCOMPARE(settings_text::layoutsFromGnomeSources(QStringLiteral("[('xkb', 'us')]")),
             QStringLiteral("us"));
    QCOMPARE(settings_text::layoutsFromGnomeSources(
                 QStringLiteral("[('xkb', 'us'), ('xkb', 'kr+kr104')]")),
             QStringLiteral("us,kr+kr104"));

    // The order is the order they were in: the first is the one the desktop
    // starts on.
    QCOMPARE(settings_text::layoutsFromGnomeSources(
                 QStringLiteral("[('xkb', 'kr+kr104'), ('xkb', 'us')]")),
             QStringLiteral("kr+kr104,us"));
}

/// GNOME prints an empty list as "@a(ss) []", which used to be read as a
/// layout named "@ass" - recorded as present, and written onto the new machine
/// as an input source that does not exist.
void LinuxSettingsTest::anEmptyInputSourceListIsNotALayout() {
    QCOMPARE(settings_text::layoutsFromGnomeSources(QStringLiteral("@a(ss) []")), QString());
    QCOMPARE(settings_text::layoutsFromGnomeSources(QStringLiteral("[]")), QString());
    QCOMPARE(settings_text::layoutsFromGnomeSources(QString()), QString());
    QCOMPARE(settings_text::layoutsFromGnomeSources(QStringLiteral("nonsense")), QString());
}

/// An ibus entry is an input method rather than a layout, and the writing side
/// puts everything back as ('xkb', ...) - so carrying one across writes an xkb
/// layout called "anthy", which no keyboard has.
void LinuxSettingsTest::ignoresInputMethodsBecauseTheyCannotBeWrittenBack() {
    QCOMPARE(settings_text::layoutsFromGnomeSources(
                 QStringLiteral("[('xkb', 'gb'), ('ibus', 'anthy')]")),
             QStringLiteral("gb"));
    QCOMPARE(settings_text::layoutsFromGnomeSources(QStringLiteral("[('ibus', 'anthy')]")),
             QString());
}

/// Whether gsettings prints a duration as "900" or as "uint32 900" is a
/// property of that key's type in that version of that schema. There were two
/// rules, one per key, and the one that guessed wrong read as "this machine
/// has no setting for it" rather than as anything being amiss.
void LinuxSettingsTest::readsADurationWhicheverWayItIsPrinted() {
    QCOMPARE(settings_text::minutesFromGnomeSeconds(QStringLiteral("900")), QStringLiteral("15"));
    QCOMPARE(settings_text::minutesFromGnomeSeconds(QStringLiteral("uint32 900")),
             QStringLiteral("15"));
    QCOMPARE(settings_text::minutesFromGnomeSeconds(QStringLiteral("  int32 1800  ")),
             QStringLiteral("30"));

    // Never is a real answer and is zero minutes, not "no setting".
    QCOMPARE(settings_text::minutesFromGnomeSeconds(QStringLiteral("0")), QStringLiteral("0"));
}

void LinuxSettingsTest::refusesADurationThatIsNotOne() {
    QCOMPARE(settings_text::minutesFromGnomeSeconds(QString()), QString());
    QCOMPARE(settings_text::minutesFromGnomeSeconds(QStringLiteral("nothing")), QString());
    QCOMPARE(settings_text::minutesFromGnomeSeconds(QStringLiteral("-60")), QString());
}

void LinuxSettingsTest::saysWhichThemeADesktopIsUsing() {
    QCOMPARE(
        settings_text::themeFromGnome(QStringLiteral("prefer-dark"), QStringLiteral("Adwaita")),
        QStringLiteral("dark"));
    QCOMPARE(
        settings_text::themeFromGnome(QStringLiteral("prefer-light"), QStringLiteral("Adwaita")),
        QStringLiteral("light"));

    // Before GNOME 42 there was no colour-scheme key and the theme's name was
    // the only thing that said.
    QCOMPARE(settings_text::themeFromGnome(QStringLiteral("default"), QStringLiteral("Yaru-dark")),
             QStringLiteral("dark"));
    QCOMPARE(settings_text::themeFromGnome(QStringLiteral("default"), QStringLiteral("Yaru")),
             QStringLiteral("light"));

    // And a desktop that says neither is not a desktop set to light.
    QCOMPARE(settings_text::themeFromGnome(QString(), QString()), QString());
}

/// "sr_RS.UTF-8@latin" is Serbian written in the Latin alphabet. Put back as
/// "sr_RS@latin.UTF-8" it names no locale at all, and a machine given a locale
/// that does not exist falls back to C - so the person's language, their date
/// format and their sort order all quietly revert.
void LinuxSettingsTest::keepsALocaleModifierWhereItBelongs() {
    QCOMPARE(settings_text::normaliseLocale(QStringLiteral("sr_RS.UTF-8@latin")),
             QStringLiteral("sr-RS@latin"));
    QCOMPARE(settings_text::toPosixLocale(QStringLiteral("sr-RS@latin")),
             QStringLiteral("sr_RS.UTF-8@latin"));
}

void LinuxSettingsTest::aLocaleSurvivesTheRoundTrip() {
    for (const QString& locale : {QStringLiteral("ko_KR.UTF-8"), QStringLiteral("en_GB.UTF-8"),
                                  QStringLiteral("sr_RS.UTF-8@latin"), QStringLiteral("C")}) {
        const QString stored = settings_text::normaliseLocale(locale);
        const QString applied = settings_text::toPosixLocale(stored);

        // C has no encoding of its own to keep, so it is the one that changes.
        const QString expected = locale == QLatin1String("C") ? QStringLiteral("C.UTF-8") : locale;
        QCOMPARE(applied, expected);
    }

    QCOMPARE(settings_text::toPosixLocale(QString()), QString());
}

void LinuxSettingsTest::readsTheZoneOutOfTheLink() {
    QCOMPARE(settings_text::timezoneFromLink(QStringLiteral("/usr/share/zoneinfo/Asia/Seoul")),
             QStringLiteral("Asia/Seoul"));
    QCOMPARE(settings_text::timezoneFromLink(QStringLiteral("../usr/share/zoneinfo/UTC")),
             QStringLiteral("UTC"));

    // A machine whose /etc/localtime is a real file rather than a link has
    // nothing to say here, and saying nothing is right.
    QCOMPARE(settings_text::timezoneFromLink(QString()), QString());
    QCOMPARE(settings_text::timezoneFromLink(QStringLiteral("/etc/localtime")), QString());
}

void LinuxSettingsTest::stripsTheQuotesGsettingsAdds() {
    QCOMPARE(settings_text::unquote(QStringLiteral("'prefer-dark'")),
             QStringLiteral("prefer-dark"));
    QCOMPARE(settings_text::unquote(QStringLiteral("  \"Adwaita\"  ")), QStringLiteral("Adwaita"));
    QCOMPARE(settings_text::unquote(QStringLiteral("''")), QString());
    QCOMPARE(settings_text::unquote(QStringLiteral("true")), QStringLiteral("true"));

    // One quote is not a pair, and taking it for one loses the character.
    QCOMPARE(settings_text::unquote(QStringLiteral("'")), QStringLiteral("'"));
}

}  // namespace transmit::platform

QTEST_GUILESS_MAIN(transmit::platform::LinuxSettingsTest)
#include "LinuxSettingsTest.moc"
