#include "core/update/UpdateManifest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

#include <algorithm>

namespace transmit::core {
namespace {

/// The largest feed that will be read at all. A manifest is a list of a few
/// releases; anything approaching a megabyte is either a mistake or somebody
/// seeing how much memory the parser will ask for.
constexpr int kLargestManifest = 256 * 1024;

/// No release carries a file bigger than this. The installers are tens of
/// megabytes; the cap is generous enough never to be met by a real build and
/// small enough that a forged size cannot ask a machine to reserve a disk.
constexpr quint64 kLargestArtifact = 2ULL * 1024 * 1024 * 1024;

/// 32 bytes, written as 64 hexadecimal characters, in either case.
std::optional<QByteArray> readDigest(const QJsonValue& value, int bytes) {
    if (value.isUndefined() || value.isNull()) {
        return QByteArray{};
    }
    if (!value.isString()) {
        return std::nullopt;
    }
    const QString text = value.toString();
    if (text.size() != bytes * 2) {
        return std::nullopt;
    }
    static const QRegularExpression hexOnly(QStringLiteral("^[0-9a-fA-F]+$"));
    if (!hexOnly.match(text).hasMatch()) {
        return std::nullopt;
    }
    return QByteArray::fromHex(text.toLatin1());
}

std::optional<QDateTime> readTime(const QJsonValue& value, bool required) {
    if (value.isUndefined() || value.isNull()) {
        return required ? std::nullopt : std::optional<QDateTime>{QDateTime{}};
    }
    if (!value.isString()) {
        return std::nullopt;
    }
    const QDateTime when = QDateTime::fromString(value.toString(), Qt::ISODate);
    if (!when.isValid()) {
        return std::nullopt;
    }
    return when.toUTC();
}

/// A name that is going to end up in a comparison or a file path. Letters,
/// digits, a dash and an underscore, and nothing else - no separators, no
/// dots, nothing that could climb out of a directory later.
bool isPlainName(const QString& text) {
    if (text.isEmpty() || text.size() > 32) {
        return false;
    }
    return std::all_of(text.cbegin(), text.cend(), [](QChar character) {
        return character.isLetterOrNumber() || character == u'-' || character == u'_';
    });
}

QString readArtifact(const QJsonObject& object, UpdateArtifact& artifact) {
    artifact.platform = object.value(QStringLiteral("platform")).toString();
    artifact.arch = object.value(QStringLiteral("arch")).toString();
    artifact.kind = object.value(QStringLiteral("kind")).toString();
    if (!isPlainName(artifact.platform) || !isPlainName(artifact.arch) ||
        !isPlainName(artifact.kind)) {
        return QStringLiteral("an artifact has a platform, arch or kind that is not a plain name");
    }

    artifact.url = QUrl(object.value(QStringLiteral("url")).toString(), QUrl::StrictMode);
    if (!artifact.url.isValid() || artifact.url.scheme() != QLatin1String("https")) {
        return QStringLiteral("the %1 artifact's url is not a valid https address")
            .arg(artifact.platform);
    }

    const QJsonValue size = object.value(QStringLiteral("size"));
    if (!size.isDouble()) {
        return QStringLiteral("the %1 artifact has no size").arg(artifact.platform);
    }
    const double asDouble = size.toDouble();
    if (asDouble < 1 || asDouble > static_cast<double>(kLargestArtifact)) {
        return QStringLiteral("the %1 artifact's size is not believable").arg(artifact.platform);
    }
    artifact.size = static_cast<quint64>(asDouble);

    const auto blake2b = readDigest(object.value(QStringLiteral("blake2b")), 32);
    if (!blake2b || blake2b->isEmpty()) {
        return QStringLiteral("the %1 artifact has no BLAKE2b digest, so nothing could check it")
            .arg(artifact.platform);
    }
    artifact.blake2b = *blake2b;

    const auto sha256 = readDigest(object.value(QStringLiteral("sha256")), 32);
    if (!sha256) {
        return QStringLiteral("the %1 artifact's sha256 is not 64 hexadecimal characters")
            .arg(artifact.platform);
    }
    artifact.sha256 = *sha256;
    return {};
}

QString readRelease(const QJsonObject& object, UpdateRelease& release) {
    const auto version =
        Version::parse(object.value(QStringLiteral("version")).toString().toStdString());
    if (!version) {
        return QStringLiteral("a release has no readable version");
    }
    release.version = *version;

    const QJsonValue severity = object.value(QStringLiteral("severity"));
    if (!severity.isString()) {
        return QStringLiteral("release %1 does not say how severe it is")
            .arg(release.version.toString().c_str());
    }
    const auto read = severityFromString(severity.toString());
    if (!read) {
        // Not defaulted to Normal on purpose. A future manifest that invents
        // "emergency" must not be quietly downgraded to a suggestion by an
        // older build, and must not be quietly promoted either.
        return QStringLiteral("release %1 has a severity this build does not know: %2")
            .arg(release.version.toString().c_str(), severity.toString());
    }
    release.severity = *read;

    const QJsonValue unsafeBelow = object.value(QStringLiteral("unsafeBelow"));
    if (unsafeBelow.isString()) {
        const auto floor = Version::parse(unsafeBelow.toString().toStdString());
        if (!floor) {
            return QStringLiteral("release %1 has an unreadable unsafeBelow")
                .arg(release.version.toString().c_str());
        }
        release.unsafeBelow = *floor;
    } else if (!unsafeBelow.isUndefined() && !unsafeBelow.isNull()) {
        return QStringLiteral("release %1 has an unsafeBelow that is not a version")
            .arg(release.version.toString().c_str());
    }

    release.notes = object.value(QStringLiteral("notes")).toString();
    const QString notesUrl = object.value(QStringLiteral("notesUrl")).toString();
    if (!notesUrl.isEmpty()) {
        release.notesUrl = QUrl(notesUrl, QUrl::StrictMode);
        if (!release.notesUrl.isValid() || release.notesUrl.scheme() != QLatin1String("https")) {
            return QStringLiteral("release %1 has notes at something that is not an https address")
                .arg(release.version.toString().c_str());
        }
    }

    const auto published = readTime(object.value(QStringLiteral("published")), false);
    if (!published) {
        return QStringLiteral("release %1 has an unreadable published time")
            .arg(release.version.toString().c_str());
    }
    release.published = *published;

    const QJsonValue artifacts = object.value(QStringLiteral("artifacts"));
    if (!artifacts.isArray()) {
        return QStringLiteral("release %1 lists no artifacts")
            .arg(release.version.toString().c_str());
    }
    for (const QJsonValue& entry : artifacts.toArray()) {
        if (!entry.isObject()) {
            return QStringLiteral("release %1 has an artifact that is not an object")
                .arg(release.version.toString().c_str());
        }
        UpdateArtifact artifact;
        const QString problem = readArtifact(entry.toObject(), artifact);
        if (!problem.isEmpty()) {
            return problem;
        }
        release.artifacts.append(artifact);
    }
    if (release.artifacts.isEmpty()) {
        return QStringLiteral("release %1 has an empty artifact list")
            .arg(release.version.toString().c_str());
    }
    return {};
}

}  // namespace

QString describe(UpdateSeverity severity) {
    switch (severity) {
        case UpdateSeverity::Normal:
            return QStringLiteral("normal");
        case UpdateSeverity::Important:
            return QStringLiteral("important");
        case UpdateSeverity::Critical:
            return QStringLiteral("critical");
    }
    return QStringLiteral("normal");
}

std::optional<UpdateSeverity> severityFromString(QStringView text) {
    if (text == u"normal") {
        return UpdateSeverity::Normal;
    }
    if (text == u"important") {
        return UpdateSeverity::Important;
    }
    if (text == u"critical") {
        return UpdateSeverity::Critical;
    }
    return std::nullopt;
}

bool UpdateArtifact::isUsable() const {
    return url.isValid() && url.scheme() == QLatin1String("https") && size > 0 &&
           blake2b.size() == 32;
}

std::optional<UpdateArtifact> UpdateRelease::artifactFor(const QString& platform,
                                                         const QString& arch,
                                                         const QString& kind) const {
    for (const UpdateArtifact& artifact : artifacts) {
        if (artifact.platform == platform && artifact.arch == arch && artifact.kind == kind &&
            artifact.isUsable()) {
            return artifact;
        }
    }
    return std::nullopt;
}

std::optional<UpdateRelease> UpdateManifest::newestAfter(const Version& current) const {
    std::optional<UpdateRelease> best;
    for (const UpdateRelease& release : releases) {
        if (release.version <= current) {
            continue;
        }
        if (!best || best->version < release.version) {
            best = release;
        }
    }
    return best;
}

bool UpdateManifest::hasExpired(const QDateTime& now) const {
    return expires.isValid() && now.isValid() && now > expires;
}

UpdateManifestReading readUpdateManifest(const QByteArray& json) {
    UpdateManifestReading reading;

    if (json.isEmpty()) {
        reading.problem = QStringLiteral("the update feed was empty");
        return reading;
    }
    if (json.size() > kLargestManifest) {
        reading.problem = QStringLiteral(
                              "the update feed is %1 bytes, which is far more than a "
                              "list of releases could be")
                              .arg(json.size());
        return reading;
    }

    QJsonParseError failure{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &failure);
    if (failure.error != QJsonParseError::NoError) {
        reading.problem =
            QStringLiteral("the update feed is not JSON: %1").arg(failure.errorString());
        return reading;
    }
    if (!document.isObject()) {
        reading.problem = QStringLiteral("the update feed is not an object");
        return reading;
    }

    const QJsonObject root = document.object();
    UpdateManifest manifest;

    const QJsonValue schema = root.value(QStringLiteral("schema"));
    if (!schema.isDouble()) {
        reading.problem = QStringLiteral("the update feed does not say which schema it is");
        return reading;
    }
    manifest.schema = schema.toInt();
    if (manifest.schema != UpdateManifest::kSchema) {
        // Refused rather than read as far as it goes. A newer feed may say
        // something this build cannot see - that a release is critical, that an
        // artifact has been withdrawn - and reading the parts it recognises
        // would be acting on half a sentence.
        reading.problem = QStringLiteral(
                              "the update feed is schema %1 and this build reads "
                              "schema %2, so it cannot be trusted to mean what it says")
                              .arg(manifest.schema)
                              .arg(UpdateManifest::kSchema);
        return reading;
    }

    const auto generated = readTime(root.value(QStringLiteral("generated")), true);
    if (!generated) {
        reading.problem = QStringLiteral("the update feed has no readable generated time");
        return reading;
    }
    manifest.generated = *generated;

    const auto expires = readTime(root.value(QStringLiteral("expires")), false);
    if (!expires) {
        reading.problem = QStringLiteral("the update feed has an unreadable expiry");
        return reading;
    }
    manifest.expires = *expires;

    const QJsonValue releases = root.value(QStringLiteral("releases"));
    if (!releases.isArray()) {
        reading.problem = QStringLiteral("the update feed has no releases array");
        return reading;
    }
    for (const QJsonValue& entry : releases.toArray()) {
        if (!entry.isObject()) {
            reading.problem = QStringLiteral("the update feed has a release that is not an object");
            return reading;
        }
        UpdateRelease release;
        const QString problem = readRelease(entry.toObject(), release);
        if (!problem.isEmpty()) {
            reading.problem = problem;
            return reading;
        }
        manifest.releases.append(release);
    }

    reading.manifest = manifest;
    return reading;
}

}  // namespace transmit::core
