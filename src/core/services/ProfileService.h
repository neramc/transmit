#pragma once

#include <QList>
#include <QString>

#include "core/continuity/ContinuityTypes.h"

namespace transmit::core {

/// A named starting point for a capture. Profiles exist because the honest
/// answer to "what should I take with me?" depends on why the user is moving,
/// and picking folders by hand on a first run is a poor experience.
struct CaptureProfile {
    QString id;
    QString displayName;
    QString description;
    CaptureSelection selection;

    /// Rough guidance shown next to the profile before a scan has run.
    QString sizeHint;
};

/// Supplies the built-in profiles. Application state and settings roots are
/// added by their own domains at capture time; a profile only decides which
/// domains and which user folders take part.
class ProfileService {
public:
    [[nodiscard]] static QList<CaptureProfile> builtInProfiles();

    /// Looks a profile up by id, falling back to the full one.
    [[nodiscard]] static CaptureProfile profileById(const QString& id);

    /// The everything-that-can-travel profile, and the default in the UI.
    [[nodiscard]] static CaptureProfile fullContinuity();
};

}  // namespace transmit::core
