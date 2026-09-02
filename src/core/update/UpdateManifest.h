#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QUrl>

#include <optional>

#include "core/update/Version.h"

namespace transmit::core {

/// How badly a release wants to be installed.
///
/// Only Critical changes what happens without being asked, and only ever after
/// the signature has been checked. Everything else is a suggestion the person
/// using the program answers.
enum class UpdateSeverity {
    Normal,     ///< features and ordinary fixes
    Important,  ///< a fix worth going out of your way for, still offered
    Critical,   ///< data loss or a vulnerability: installed without asking
};

[[nodiscard]] QString describe(UpdateSeverity severity);
[[nodiscard]] std::optional<UpdateSeverity> severityFromString(QStringView text);

/// One downloadable file belonging to a release.
struct UpdateArtifact {
    QString platform;  ///< "linux", "windows", "macos"
    QString arch;      ///< "x86_64", "arm64"
    QString kind;      ///< "appimage", "setup", "portable", "dmg"
    QUrl url;
    quint64 size = 0;

    /// BLAKE2b-256 of the file, as raw bytes. This is what a download is
    /// checked against: the project already has an implementation that no
    /// build option can remove, so verification cannot quietly become
    /// optional on a machine without OpenSSL.
    QByteArray blake2b;

    /// SHA-256 of the same file, for checking by hand against SHA256SUMS.
    /// Never the only thing relied on, and absent from older manifests.
    QByteArray sha256;

    [[nodiscard]] bool isUsable() const;
};

/// One published version.
struct UpdateRelease {
    Version version;
    UpdateSeverity severity = UpdateSeverity::Normal;

    /// Which versions this release considers unsafe. Zero means "every version
    /// below this one", which is what a critical release almost always means.
    /// Stating it lets a fix say that only 0.3.x was affected, so somebody on
    /// 0.2.9 is offered the update rather than given it.
    Version unsafeBelow;

    QString notes;
    QUrl notesUrl;
    QDateTime published;
    QList<UpdateArtifact> artifacts;

    /// The artifact for this platform and processor, if the release has one.
    [[nodiscard]] std::optional<UpdateArtifact> artifactFor(const QString& platform,
                                                            const QString& arch,
                                                            const QString& kind) const;
};

/// The whole feed.
struct UpdateManifest {
    static constexpr int kSchema = 1;

    int schema = 0;
    QDateTime generated;

    /// When this feed stops being believed. An attacker who can serve stale
    /// files cannot forge a manifest, but can keep replaying an old one to
    /// hide the release that fixes the hole they are using; an expiry puts a
    /// limit on how long that works. Absent in a feed that does not set one,
    /// and then only the age is reported, never enforced - a project that goes
    /// quiet for a season should not lock its users out of updating.
    QDateTime expires;

    QList<UpdateRelease> releases;

    /// The newest release strictly newer than `current`, or nothing.
    /// Never returns something older or equal: an updater that can move
    /// somebody backwards is a way to reinstall a fixed vulnerability.
    [[nodiscard]] std::optional<UpdateRelease> newestAfter(const Version& current) const;

    [[nodiscard]] bool hasExpired(const QDateTime& now) const;
};

/// What came of reading a manifest. Parsing is separated from fetching so the
/// rules can be tested without a network, which is most of what there is to
/// get wrong.
struct UpdateManifestReading {
    std::optional<UpdateManifest> manifest;
    QString problem;  ///< empty when the manifest was read

    [[nodiscard]] bool ok() const { return manifest.has_value(); }
};

/// Reads the feed. Refuses anything it does not fully understand rather than
/// filling in a default: every field here decides either what gets downloaded
/// or whether it gets installed without asking.
[[nodiscard]] UpdateManifestReading readUpdateManifest(const QByteArray& json);

}  // namespace transmit::core
