#include "i18n/Language.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QSettings>
#include <QStandardPaths>

namespace transmit::app {
namespace {

Q_LOGGING_CATEGORY(logLanguage, "transmit.language")

constexpr const char* kSettingsKey = "interface/language";
constexpr const char* kSystem = "system";

/// Where the built translations are. qt_add_translations puts them here.
QString catalogueDirectory() {
    return QStringLiteral(":/i18n");
}

/// The names people call their own languages by, rather than what English
/// calls them. QLocale can produce these, but only for a locale it knows in
/// the way it knows it; a table of the languages actually shipped is shorter
/// than the code to talk QLocale into the same answer.
const QMap<QString, QString>& endonyms() {
    static const QMap<QString, QString> names = {
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("ko"), QStringLiteral("한국어")},
    };
    return names;
}

}  // namespace

Language::Language(QObject* parent) : QObject(parent), current_(QString::fromLatin1(kSystem)) {}

Language::~Language() = default;

Language& Language::instance() {
    static Language language;
    return language;
}

Language* Language::create(QQmlEngine* engine, QJSEngine* scriptEngine) {
    Q_UNUSED(scriptEngine);
    Language& language = instance();
    // The engine that has to be told to reread every string when the language
    // changes. Without it the setting appears to do nothing until the next
    // start, which reads as the setting being broken.
    language.follow(qobject_cast<QQmlApplicationEngine*>(engine));
    QJSEngine::setObjectOwnership(&language, QJSEngine::CppOwnership);
    return &language;
}

QStringList Language::available() {
    // English is not a catalogue: it is what the strings in the source are
    // written in, and a translation of English into English would be six
    // hundred lines of a sentence repeating itself. It is offered anyway,
    // because somebody on a Korean machine has to be able to ask for it.
    QStringList codes{QString::fromLatin1(kSystem), QStringLiteral("en")};
    const QDir directory(catalogueDirectory());
    for (const QString& name :
         directory.entryList({QStringLiteral("transmit_*.qm")}, QDir::Files, QDir::Name)) {
        QString code = name;
        code.remove(QStringLiteral("transmit_"));
        code.chop(static_cast<int>(QStringLiteral(".qm").size()));
        if (!code.isEmpty()) {
            codes.append(code);
        }
    }
    return codes;
}

QString Language::nameOf(const QString& code) {
    if (code == QLatin1String(kSystem)) {
        // Named for what it does rather than for a language, because what it
        // does is defer to a choice made somewhere else.
        return QCoreApplication::translate("Language", "Same as this computer");
    }
    if (const auto it = endonyms().constFind(code); it != endonyms().constEnd()) {
        return it.value();
    }
    const QString native = QLocale(code).nativeLanguageName();
    return native.isEmpty() ? code : native;
}

QString Language::resolve(const QString& code) {
    if (code != QLatin1String(kSystem)) {
        return code;
    }
    // uiLanguages rather than name(): somebody with a machine set to Korean
    // but a region of Canada gets "ko-CA", and the language is the half that
    // decides which translation to load.
    const QStringList preferred = QLocale::system().uiLanguages();
    for (const QString& candidate : preferred) {
        const QString language = candidate.left(candidate.indexOf(QLatin1Char('-')));
        if (available().contains(language)) {
            return language;
        }
    }
    return QStringLiteral("en");
}

void Language::use(const QString& code) {
    const QString wanted = available().contains(code) ? code : QString::fromLatin1(kSystem);
    const QString language = resolve(wanted);

    // Removed before loading. A translator that is still installed keeps
    // answering, so switching from Korean to English with the old one in place
    // leaves every string that English does not override in Korean.
    if (application_) {
        QCoreApplication::removeTranslator(application_.get());
    }
    if (qt_) {
        QCoreApplication::removeTranslator(qt_.get());
    }

    application_ = std::make_unique<QTranslator>();
    if (application_->load(QStringLiteral("transmit_%1").arg(language), catalogueDirectory())) {
        QCoreApplication::installTranslator(application_.get());
    } else {
        // Not a failure: English is the language the strings are written in,
        // so having no catalogue for it is the normal case.
        if (language != QStringLiteral("en")) {
            qCWarning(logLanguage) << "no translation for" << language;
        }
        application_.reset();
    }

    // Qt's own strings - the buttons in a dialog, the names of the standard
    // folders. They live in Qt's catalogue, not this program's, and a window
    // with translated labels and untranslated buttons looks broken in a way
    // that is hard to describe to whoever built it.
    qt_ = std::make_unique<QTranslator>();
    const QString qtCatalogues = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (qt_->load(QLocale(language), QStringLiteral("qtbase"), QStringLiteral("_"), qtCatalogues)) {
        QCoreApplication::installTranslator(qt_.get());
    } else {
        qt_.reset();
    }

    current_ = wanted;
    QSettings().setValue(QLatin1String(kSettingsKey), wanted);

    // Said out loud, because "the program is in the wrong language" is a
    // report with three possible causes - the setting, the machine's locale,
    // and a catalogue that was not built in - and this line separates them.
    qCInfo(logLanguage).nospace() << "interface language: " << language << " (setting: " << wanted
                                  << ", catalogue: " << (application_ ? "loaded" : "none")
                                  << ", Qt's own: " << (qt_ ? "loaded" : "none") << ")";

    if (engine_ != nullptr) {
        engine_->retranslate();
    }
    emit changed();
}

void Language::applyStored() {
    use(QSettings().value(QLatin1String(kSettingsKey), QString::fromLatin1(kSystem)).toString());
}

}  // namespace transmit::app
