#pragma once

#include <QColor>
#include <QObject>
#include <QQmlEngine>
#include <QString>

namespace transmit::app {

/// Which desktop this is running on, and the look that belongs to it.
///
/// A program that looks the same everywhere looks out of place everywhere. The
/// corners, the height of a button, the order of the buttons in a dialog and
/// the font are not decoration - they are what makes a window read as part of
/// the system it is on, and every desktop has decided them differently.
///
/// What this does not do is imitate a native toolkit. The interface is Qt
/// Quick with a design system of its own, and pretending otherwise produces
/// something that is nearly right, which is worse than something that is
/// clearly itself. What it does is take the handful of measurements a person
/// notices immediately from wherever they are running it.
class DesktopProfile : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// "windows11", "windows10", "macos", "gnome", "kde", "xfce", "cosmic",
    /// or "default" where nothing is known.
    Q_PROPERTY(QString detected READ detected CONSTANT)

    /// What is being used, which is `detected` unless the setting overrides it.
    Q_PROPERTY(QString current READ current NOTIFY changed)

    /// Every profile that can be chosen, "system" first.
    Q_PROPERTY(QStringList available READ available CONSTANT)

    /// The desktop's own highlight colour, where it can be read without
    /// starting a process. Invalid when it cannot, and the brand colour is
    /// used instead - a wrong accent is worse than a deliberate one.
    Q_PROPERTY(QColor systemAccent READ systemAccent CONSTANT)

public:
    static DesktopProfile& instance();
    static DesktopProfile* create(QQmlEngine* engine, QJSEngine* scriptEngine);

    [[nodiscard]] QString detected() const { return detected_; }
    [[nodiscard]] QString current() const { return current_; }
    [[nodiscard]] QColor systemAccent() const { return accent_; }

    [[nodiscard]] static QStringList available();

    /// The name to show for a profile, translated.
    Q_INVOKABLE [[nodiscard]] static QString nameOf(const QString& profile);

    /// Uses a profile, or "system" to go back to what was detected.
    Q_INVOKABLE void use(const QString& profile);

signals:
    void changed();

private:
    explicit DesktopProfile(QObject* parent = nullptr);

    [[nodiscard]] static QString detect();
    [[nodiscard]] static QColor readSystemAccent(const QString& profile);

    QString detected_;
    QString current_;
    QColor accent_;
};

}  // namespace transmit::app
