#pragma once

#include <QDateTime>
#include <QString>

#include <optional>

#include "core/update/InstallKind.h"
#include "core/update/UpdateManifest.h"
#include "core/update/Version.h"

namespace transmit::core {

/// What the person using Transmit asked for. Only ever consulted for updates
/// that are not critical: a fix for something that loses their files or lets
/// somebody else read them is not a matter of taste.
enum class UpdatePreference {
    Manual,     ///< never look on its own; check when asked
    Notify,     ///< look, and say when there is something
    Automatic,  ///< look, and install it
};

[[nodiscard]] QString describe(UpdatePreference preference);
[[nodiscard]] std::optional<UpdatePreference> preferenceFromString(QStringView text);
[[nodiscard]] QString toString(UpdatePreference preference);

/// Everything the decision depends on, gathered in one place so the rules can
/// be exercised without a network, a signing key or an installed copy. Every
/// interesting case in this feature is a combination of these, and the ones
/// that matter most - a critical release on an unsigned feed, a critical
/// release on a Flatpak - are exactly the ones that are hard to arrange for
/// real.
struct UpdateSituation {
    Version current;
    InstallKind installKind = InstallKind::Unknown;

    /// The feed's signature checked out against a key this build trusts.
    /// Nothing is ever downloaded or installed when this is false.
    bool feedVerified = false;

    /// The build was compiled with the updater in it.
    bool updaterEnabled = true;

    UpdatePreference preference = UpdatePreference::Notify;
    QDateTime now;

    /// Which artifact this machine would need.
    QString platform;
    QString arch;
    QString artifactKind;
};

enum class UpdateAction {
    NothingToDo,   ///< already running the newest release
    CannotCheck,   ///< the feed could not be believed, or there is no updater
    TellThemOnly,  ///< there is something newer, and this copy must not install it
    Offer,         ///< ask, then install
    InstallNow,    ///< install without asking
};

[[nodiscard]] QString describe(UpdateAction action);

struct UpdateDecision {
    UpdateAction action = UpdateAction::NothingToDo;
    std::optional<UpdateRelease> release;
    std::optional<UpdateArtifact> artifact;

    /// The release fixes something the running version is exposed to. Stays
    /// true even when the action is only TellThemOnly, because a copy that may
    /// not install the fix still has to say loudly that it needs it.
    bool mandatory = false;

    /// Always filled in, including on success. Every one of these ends up in
    /// front of somebody - in a log, in the interface, or on the command line -
    /// and "no update" and "could not check" must never look the same.
    QString reason;
};

/// Works out what to do. Pure: no network, no disk, no clock of its own.
[[nodiscard]] UpdateDecision decideOnUpdate(const UpdateManifest& manifest,
                                            const UpdateSituation& situation);

/// The platform, processor and artifact kind this build would need, filled in
/// from what it was compiled for.
[[nodiscard]] UpdateSituation situationForThisBuild();

}  // namespace transmit::core
