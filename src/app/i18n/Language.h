#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QTranslator>

#include <memory>

class QQmlApplicationEngine;

namespace transmit::app {

/// Which language the interface is in, and the machinery that puts it there.
///
/// Three things have to line up for a person to read this program in their own
/// language: the translation has to be built and carried in the binary, it has
/// to be loaded before the first window is made, and Qt's own strings - the
/// text in a file dialog, the buttons in a message box - have to be loaded too,
/// from a different catalogue that ships with Qt. Missing any one of them looks
/// like a half-translated program rather than an error.
class Language : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString current READ current NOTIFY changed)
    Q_PROPERTY(QStringList available READ available CONSTANT)

public:
    /// The one instance. Reachable from C++ before there is an engine, because
    /// the language has to be loaded before the first window is built, and
    /// from QML afterwards as the same object rather than a second one that
    /// would answer differently.
    static Language& instance();

    /// What the QML engine calls to get the singleton. Hands back the instance
    /// above rather than making one, and keeps ownership in C++ so the engine
    /// does not delete something main() is still holding.
    static Language* create(QQmlEngine* engine, QJSEngine* scriptEngine);

    ~Language() override;

    /// The language codes there are translations for, "system" first.
    ///
    /// Read from what was actually built rather than from a list written down
    /// here: a translation added to the build and forgotten in a list is a
    /// language nobody can choose.
    [[nodiscard]] static QStringList available();

    /// The name a person would recognise, in that language: "한국어", not
    /// "Korean". Somebody looking for their own language is looking for the
    /// word they call it by.
    Q_INVOKABLE [[nodiscard]] static QString nameOf(const QString& code);

    [[nodiscard]] QString current() const { return current_; }

    /// Loads a language and makes every string in the interface follow.
    ///
    /// "system" means whatever the machine is set to, which is what somebody
    /// who has never opened the setting should get.
    Q_INVOKABLE void use(const QString& code);

    /// Applies the language chosen last time, or the machine's, at start-up.
    void applyStored();

    /// The engine to retranslate when the language changes. Without it a
    /// change takes effect the next time the program starts, which reads as
    /// the setting not working.
    void follow(QQmlApplicationEngine* engine) { engine_ = engine; }

signals:
    void changed();

private:
    explicit Language(QObject* parent = nullptr);

    [[nodiscard]] static QString resolve(const QString& code);

    QString current_;
    std::unique_ptr<QTranslator> application_;
    std::unique_ptr<QTranslator> qt_;
    QQmlApplicationEngine* engine_ = nullptr;
};

}  // namespace transmit::app
