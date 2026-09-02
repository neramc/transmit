#include "core/update/UpdateDecision.h"

#include <QCoreApplication>
#include <QSysInfo>

namespace transmit::core {

QString describe(UpdatePreference preference) {
    switch (preference) {
        case UpdatePreference::Manual:
            return QStringLiteral("only when I ask");
        case UpdatePreference::Notify:
            return QStringLiteral("tell me when there is one");
        case UpdatePreference::Automatic:
            return QStringLiteral("install them");
    }
    return QStringLiteral("tell me when there is one");
}

QString toString(UpdatePreference preference) {
    switch (preference) {
        case UpdatePreference::Manual:
            return QStringLiteral("manual");
        case UpdatePreference::Notify:
            return QStringLiteral("notify");
        case UpdatePreference::Automatic:
            return QStringLiteral("automatic");
    }
    return QStringLiteral("notify");
}

std::optional<UpdatePreference> preferenceFromString(QStringView text) {
    if (text == u"manual") {
        return UpdatePreference::Manual;
    }
    if (text == u"notify") {
        return UpdatePreference::Notify;
    }
    if (text == u"automatic") {
        return UpdatePreference::Automatic;
    }
    return std::nullopt;
}

QString describe(UpdateAction action) {
    switch (action) {
        case UpdateAction::NothingToDo:
            return QStringLiteral("nothing to do");
        case UpdateAction::CannotCheck:
            return QStringLiteral("cannot check");
        case UpdateAction::TellThemOnly:
            return QStringLiteral("tell them only");
        case UpdateAction::Offer:
            return QStringLiteral("offer");
        case UpdateAction::InstallNow:
            return QStringLiteral("install now");
    }
    return QStringLiteral("nothing to do");
}

UpdateDecision decideOnUpdate(const UpdateManifest& manifest, const UpdateSituation& situation) {
    UpdateDecision decision;

    if (!situation.updaterEnabled) {
        decision.action = UpdateAction::CannotCheck;
        decision.reason = QStringLiteral("this build has no updater in it");
        return decision;
    }

    // A build that cannot say what version it is cannot say what is newer than
    // it, and "install the newest" without that comparison is how an updater
    // reinstalls the same release forever, or a downgrade once.
    if (situation.current.isZero()) {
        decision.action = UpdateAction::CannotCheck;
        decision.reason = QStringLiteral("this build does not know its own version");
        return decision;
    }

    if (manifest.hasExpired(situation.now)) {
        decision.action = UpdateAction::CannotCheck;
        decision.reason = QStringLiteral(
                              "the update feed expired on %1, so it may be an old copy of the feed "
                              "being replayed to hide a newer release")
                              .arg(manifest.expires.toString(Qt::ISODate));
        return decision;
    }

    const auto newest = manifest.newestAfter(situation.current);
    if (!newest) {
        decision.action = UpdateAction::NothingToDo;
        decision.reason = QStringLiteral("%1 is the newest release")
                              .arg(QString::fromStdString(situation.current.toString()));
        return decision;
    }
    decision.release = newest;

    // The floor defaults to the release's own version, which is what "this
    // fixes something dangerous" nearly always means: everything before it is
    // exposed. A release that knows better can name a narrower range.
    const Version floor = newest->unsafeBelow.isZero() ? newest->version : newest->unsafeBelow;
    decision.mandatory = newest->severity == UpdateSeverity::Critical && situation.current < floor;

    const QString describeRelease =
        QStringLiteral("%1 is available").arg(QString::fromStdString(newest->version.toString()));

    // Everything from here decides whether this copy may install it. The order
    // matters: the reason given should be the first thing that stops it, and
    // the signature is the one that must never be reachable past.
    if (!situation.feedVerified) {
        decision.action = UpdateAction::TellThemOnly;
        decision.reason =
            QStringLiteral(
                "%1, but the update feed was not signed by a key this build trusts, so "
                "nothing will be downloaded or installed")
                .arg(describeRelease);
        return decision;
    }

    if (!canReplaceItself(situation.installKind)) {
        decision.action = UpdateAction::TellThemOnly;
        decision.reason = QStringLiteral(
                              "%1, but this is %2, which updates itself through "
                              "whatever installed it")
                              .arg(describeRelease, describe(situation.installKind));
        return decision;
    }

    const auto artifact =
        newest->artifactFor(situation.platform, situation.arch, situation.artifactKind);
    if (!artifact) {
        decision.action = UpdateAction::TellThemOnly;
        decision.reason =
            QStringLiteral("%1, but it has no %2 %3 build for %4")
                .arg(describeRelease, situation.platform, situation.artifactKind, situation.arch);
        return decision;
    }
    decision.artifact = artifact;

    if (decision.mandatory) {
        decision.action = UpdateAction::InstallNow;
        decision.reason =
            QStringLiteral(
                "%1 and it fixes something every version below %2 is exposed to, so it "
                "is being installed without waiting to be asked")
                .arg(describeRelease, QString::fromStdString(floor.toString()));
        return decision;
    }

    if (situation.preference == UpdatePreference::Automatic) {
        decision.action = UpdateAction::InstallNow;
        decision.reason =
            QStringLiteral("%1 and updates are set to install themselves").arg(describeRelease);
        return decision;
    }

    decision.action = UpdateAction::Offer;
    decision.reason = describeRelease;
    return decision;
}

UpdateSituation situationForThisBuild() {
    UpdateSituation situation;

    if (const auto running = runningVersion()) {
        situation.current = *running;
    }
    situation.installKind = detectInstallKind();
    situation.now = QDateTime::currentDateTimeUtc();

#ifdef TRANSMIT_UPDATER_ENABLED
    situation.updaterEnabled = true;
#else
    situation.updaterEnabled = false;
#endif

#if defined(Q_OS_LINUX)
    situation.platform = QStringLiteral("linux");
#elif defined(Q_OS_MACOS)
    situation.platform = QStringLiteral("macos");
#elif defined(Q_OS_WIN)
    situation.platform = QStringLiteral("windows");
#endif

    // QSysInfo spells these the way the release files are named already, apart
    // from Apple silicon, which it calls arm64 and the disk image calls arm64
    // as well.
    const QString architecture = QSysInfo::currentCpuArchitecture();
    situation.arch =
        architecture == QLatin1String("x86_64") ? QStringLiteral("x86_64") : architecture;

    switch (situation.installKind) {
        case InstallKind::AppImage:
            situation.artifactKind = QStringLiteral("appimage");
            break;
        case InstallKind::WindowsInstaller:
            situation.artifactKind = QStringLiteral("setup");
            break;
        case InstallKind::WindowsPortable:
            situation.artifactKind = QStringLiteral("portable");
            break;
        case InstallKind::MacBundle:
            situation.artifactKind = QStringLiteral("dmg");
            break;
        default:
            break;
    }

    return situation;
}

}  // namespace transmit::core
