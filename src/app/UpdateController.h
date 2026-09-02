#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

#include <memory>

#include "core/update/UpdateService.h"

namespace transmit::app {

/// What the interface shows about updates.
///
/// Deliberately thin. Every rule about whether something may be installed
/// lives in core, where it is tested; this decides only how to say it.
class UpdateController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool checking READ checking NOTIFY stateChanged)
    Q_PROPERTY(bool downloading READ downloading NOTIFY stateChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY stateChanged)

    /// A fix the running version is exposed to. The interface says this
    /// loudly and does not offer to dismiss it.
    Q_PROPERTY(bool mandatory READ mandatory NOTIFY stateChanged)

    /// True while a critical fix is being installed without having been
    /// asked for, so the interface can say what is happening rather than
    /// appearing to do something on its own.
    Q_PROPERTY(bool installingUnasked READ installingUnasked NOTIFY stateChanged)

    Q_PROPERTY(bool canInstall READ canInstall NOTIFY stateChanged)
    Q_PROPERTY(bool installed READ installed NOTIFY stateChanged)
    Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY stateChanged)
    Q_PROPERTY(QString severity READ severity NOTIFY stateChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY stateChanged)
    Q_PROPERTY(QString notes READ notes NOTIFY stateChanged)
    Q_PROPERTY(QString releasesPage READ releasesPage CONSTANT)
    Q_PROPERTY(QString installKind READ installKind CONSTANT)
    Q_PROPERTY(int progressPercent READ progressPercent NOTIFY progressChanged)
    Q_PROPERTY(QString preference READ preference WRITE setPreference NOTIFY preferenceChanged)
    Q_PROPERTY(QString lastChecked READ lastChecked NOTIFY stateChanged)

public:
    explicit UpdateController(QObject* parent = nullptr);
    ~UpdateController() override;

    [[nodiscard]] bool checking() const { return checking_; }
    [[nodiscard]] bool downloading() const { return downloading_; }
    [[nodiscard]] bool updateAvailable() const;
    [[nodiscard]] bool mandatory() const;
    [[nodiscard]] bool installingUnasked() const { return installingUnasked_; }
    [[nodiscard]] bool canInstall() const;
    [[nodiscard]] bool installed() const { return installed_; }
    [[nodiscard]] QString availableVersion() const;
    [[nodiscard]] QString severity() const;
    [[nodiscard]] QString summary() const { return summary_; }
    [[nodiscard]] QString notes() const;
    [[nodiscard]] static QString releasesPage();
    [[nodiscard]] static QString installKind();
    [[nodiscard]] int progressPercent() const { return progressPercent_; }
    [[nodiscard]] static QString preference();
    void setPreference(const QString& preference);
    [[nodiscard]] QString lastChecked() const;

public slots:
    /// Looks now, whatever the preference says. This is what the button does.
    void checkNow();

    /// Looks on startup, unless the preference says not to and it was checked
    /// recently. A critical fix found this way installs itself.
    void checkQuietly();

    /// Downloads and installs what the last check found.
    void installNow();

signals:
    void stateChanged();
    void progressChanged();
    void preferenceChanged();

    /// The new version is in place and starting Transmit again will run it.
    void restartNeeded();

private:
    void beginCheck(bool quiet);
    void handleDecision(const core::UpdateDecision& decision);
    void applyStaged(const QString& path);
    void fail(const QString& problem);

    std::unique_ptr<core::UpdateService> service_;
    core::UpdateDecision decision_;
    QString summary_;
    bool checking_ = false;
    bool downloading_ = false;
    bool installed_ = false;
    bool installingUnasked_ = false;
    int progressPercent_ = 0;
};

}  // namespace transmit::app
