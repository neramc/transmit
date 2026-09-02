#include "UpdateController.h"

#include <QDateTime>
#include <QLocale>

#include "core/update/UpdateInstaller.h"
#include "core/utils/Logging.h"

namespace transmit::app {

using core::InstallKind;
using core::UpdateAction;
using core::UpdateDecision;
using core::UpdatePreference;
using core::UpdateService;

namespace {

/// A background check is not worth making more than once a day. It is a
/// request to somebody else's server on every start otherwise, and the answer
/// does not change that fast.
constexpr int kQuietCheckHours = 24;

}  // namespace

UpdateController::UpdateController(QObject* parent)
    : QObject(parent), service_(std::make_unique<UpdateService>()) {
    connect(service_.get(), &UpdateService::checked, this,
            [this](const UpdateDecision& decision) { handleDecision(decision); });
    connect(service_.get(), &UpdateService::progress, this, [this](qint64 received, qint64 total) {
        progressPercent_ = total > 0 ? static_cast<int>((received * 100) / total) : 0;
        emit progressChanged();
    });
    connect(service_.get(), &UpdateService::staged, this,
            [this](const QString& path) { applyStaged(path); });
    connect(service_.get(), &UpdateService::failed, this,
            [this](const QString& problem) { fail(problem); });
}

UpdateController::~UpdateController() = default;

bool UpdateController::updateAvailable() const {
    return decision_.action == UpdateAction::TellThemOnly ||
           decision_.action == UpdateAction::Offer || decision_.action == UpdateAction::InstallNow;
}

bool UpdateController::mandatory() const {
    return decision_.mandatory;
}

bool UpdateController::canInstall() const {
    return decision_.action == UpdateAction::Offer || decision_.action == UpdateAction::InstallNow;
}

QString UpdateController::availableVersion() const {
    return decision_.release ? QString::fromStdString(decision_.release->version.toString())
                             : QString();
}

QString UpdateController::severity() const {
    return decision_.release ? describe(decision_.release->severity) : QString();
}

QString UpdateController::notes() const {
    return decision_.release ? decision_.release->notes : QString();
}

QString UpdateController::releasesPage() {
    return UpdateService::releasesPage().toString();
}

QString UpdateController::installKind() {
    return describe(core::detectInstallKind());
}

QString UpdateController::preference() {
    return toString(UpdateService::preference());
}

void UpdateController::setPreference(const QString& preference) {
    const auto chosen = core::preferenceFromString(preference);
    if (!chosen || *chosen == UpdateService::preference()) {
        return;
    }
    UpdateService::setPreference(*chosen);
    emit preferenceChanged();
}

QString UpdateController::lastChecked() const {
    const QDateTime when = UpdateService::lastChecked();
    if (!when.isValid()) {
        return {};
    }
    return QLocale().toString(when.toLocalTime(), QLocale::ShortFormat);
}

void UpdateController::checkNow() {
    beginCheck(false);
}

void UpdateController::checkQuietly() {
    // Two ways out, both for machines that are not somebody's desktop. A
    // startup measurement should measure starting up, and a test should not
    // depend on somebody else's server being reachable.
    if (qEnvironmentVariableIsSet("TRANSMIT_NO_UPDATE_CHECK") ||
        qEnvironmentVariableIsSet("TRANSMIT_STARTUP_BENCHMARK")) {
        return;
    }

    if (UpdateService::preference() == UpdatePreference::Manual) {
        // Even then, a copy that has never checked is checked once: the
        // setting means "do not keep asking", not "never tell me a fix for
        // something dangerous exists".
        if (UpdateService::lastChecked().isValid()) {
            return;
        }
    }

    const QDateTime last = UpdateService::lastChecked();
    if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < kQuietCheckHours * 3600) {
        return;
    }
    beginCheck(true);
}

void UpdateController::beginCheck(bool quiet) {
    if (checking_ || downloading_) {
        return;
    }
    checking_ = true;
    installed_ = false;
    summary_ = quiet ? QString() : QStringLiteral("Looking...");
    emit stateChanged();
    service_->checkForUpdate();
}

void UpdateController::handleDecision(const UpdateDecision& decision) {
    decision_ = decision;
    checking_ = false;
    summary_ = decision.reason;
    emit stateChanged();

    if (decision.action != UpdateAction::InstallNow) {
        return;
    }

    // Either a critical fix, or updates set to install themselves. Both go on
    // without another question; only the first is worth saying out loud while
    // it happens.
    installingUnasked_ = decision.mandatory;
    qCWarning(logApp) << "installing an update without asking:" << decision.reason;
    installNow();
}

void UpdateController::installNow() {
    if (!canInstall() || downloading_) {
        return;
    }
    downloading_ = true;
    progressPercent_ = 0;
    summary_ = QStringLiteral("Downloading %1...").arg(availableVersion());
    emit stateChanged();
    emit progressChanged();
    service_->downloadStagedUpdate();
}

void UpdateController::applyStaged(const QString& path) {
    const InstallKind kind = core::detectInstallKind();
    const core::InstallOutcome outcome =
        core::UpdateInstaller::apply(path, core::replaceableTarget(kind), kind);

    downloading_ = false;
    installingUnasked_ = false;

    if (!outcome.applied) {
        summary_ = outcome.problem;
        if (!outcome.handedOver.isEmpty()) {
            summary_ = QStringLiteral("%1\n%2").arg(outcome.problem, outcome.handedOver);
        }
        emit stateChanged();
        return;
    }

    installed_ = true;
    summary_ =
        QStringLiteral("%1 is installed. Start Transmit again to run it.").arg(availableVersion());
    emit stateChanged();
    emit restartNeeded();
}

void UpdateController::fail(const QString& problem) {
    downloading_ = false;
    installingUnasked_ = false;
    summary_ = problem;
    qCWarning(logApp) << "update failed:" << problem;
    emit stateChanged();
}

}  // namespace transmit::app
