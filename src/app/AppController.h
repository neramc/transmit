#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QSettings>
#include <QString>
#include <memory>

#include "platform/PlatformService.h"

namespace transmit::app {

/// Application-wide state the interface binds to: which page is showing, the
/// theme, and a description of the machine Transmit is running on.
class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(QString osName READ osName CONSTANT)
    Q_PROPERTY(QString osFamily READ osFamily CONSTANT)
    Q_PROPERTY(QString hostName READ hostName CONSTANT)
    Q_PROPERTY(QString userName READ userName CONSTANT)
    Q_PROPERTY(QString homeDirectory READ homeDirectory CONSTANT)
    Q_PROPERTY(QString desktopEnvironment READ desktopEnvironment CONSTANT)
    Q_PROPERTY(QString packageManager READ packageManager CONSTANT)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(bool encryptionAvailable READ encryptionAvailable CONSTANT)
    Q_PROPERTY(bool isMac READ isMac CONSTANT)

public:
    explicit AppController(QObject* parent = nullptr);

    [[nodiscard]] QString currentPage() const { return currentPage_; }
    void setCurrentPage(const QString& page);

    /// "light", "dark" or "system".
    [[nodiscard]] QString themeMode() const { return themeMode_; }
    void setThemeMode(const QString& mode);

    [[nodiscard]] QString osName() const;
    [[nodiscard]] QString osFamily() const;
    [[nodiscard]] QString hostName() const;
    [[nodiscard]] QString userName() const;
    [[nodiscard]] QString homeDirectory() const;
    [[nodiscard]] QString desktopEnvironment() const;
    [[nodiscard]] QString packageManager() const;
    [[nodiscard]] static QString appVersion();
    [[nodiscard]] static bool encryptionAvailable();
    [[nodiscard]] bool isMac() const;

public slots:
    /// Turns a file path into the file:// URL QML's dialogs expect, and back.
    [[nodiscard]] static QString toFileUrl(const QString& path);
    [[nodiscard]] static QString fromFileUrl(const QString& url);

    /// Opens a folder in the system file manager, so the user can get to the
    /// archive straight after a capture.
    static void revealInFileManager(const QString& path);

    [[nodiscard]] static QString formatBytes(quint64 bytes);

signals:
    void currentPageChanged();
    void themeModeChanged();

private:
    std::unique_ptr<platform::PlatformService> platform_;
    platform::EnvironmentInfo environment_;
    QSettings settings_;
    QString currentPage_ = QStringLiteral("home");
    QString themeMode_;
};

}  // namespace transmit::app
