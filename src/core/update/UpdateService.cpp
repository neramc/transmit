#include "core/update/UpdateService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>

#include "core/update/UpdateSignature.h"
#include "core/utils/Logging.h"
#include "format/hash/Blake2b.h"

namespace transmit::core {
namespace {

/// The feed is a short list of releases. Anything past this is not one, and
/// the connection is cut rather than read.
constexpr qint64 kFeedCap = 256 * 1024;
constexpr qint64 kSignatureCap = 4 * 1024;

/// No release file is larger than this. The cap is checked against the size the
/// signed feed declares as well, so a server cannot answer a 60 MB request with
/// 60 GB.
constexpr qint64 kArtifactCap = 2LL * 1024 * 1024 * 1024;

/// Free space wanted before a download starts: the file, and the same again,
/// because applying it writes a second copy before removing the first.
constexpr double kSpaceHeadroom = 2.2;

/// How long an install of a particular version is remembered as having been
/// tried. Six hours.
constexpr qint64 kRepeatInstallGuardSeconds = 6 * 60 * 60;

QString settingsKey(const char* leaf) {
    return QStringLiteral("updates/") + QString::fromLatin1(leaf);
}

}  // namespace

/// Whether an address may be fetched at all: https, and a host the release is
/// published on. Defined beside the request that uses it, in UpdateFetch.cpp.
bool urlIsSafe(const QUrl& url);

UpdateService::UpdateService(QObject* parent) : QObject(parent) {}

UpdateService::~UpdateService() = default;

QUrl UpdateService::feedUrl() {
    return QUrl(QLatin1String(TRANSMIT_UPDATE_FEED), QUrl::StrictMode);
}

QUrl UpdateService::releasesPage() {
    return QUrl(QLatin1String(TRANSMIT_RELEASES_PAGE), QUrl::StrictMode);
}

UpdatePreference UpdateService::preference() {
    QSettings settings;
    const auto stored = preferenceFromString(settings.value(settingsKey("preference")).toString());
    return stored.value_or(UpdatePreference::Notify);
}

void UpdateService::setPreference(UpdatePreference preference) {
    QSettings settings;
    settings.setValue(settingsKey("preference"), toString(preference));
    settings.sync();
}

QDateTime UpdateService::lastChecked() {
    QSettings settings;
    return settings.value(settingsKey("lastChecked")).toDateTime();
}

QString UpdateService::lastInstalledVersion() {
    QSettings settings;
    return settings.value(settingsKey("lastInstalledVersion")).toString();
}

QDateTime UpdateService::lastInstalledAt() {
    QSettings settings;
    return settings.value(settingsKey("lastInstalledAt")).toDateTime();
}

void UpdateService::rememberInstalled(const QString& version) {
    QSettings settings;
    settings.setValue(settingsKey("lastInstalledVersion"), version);
    settings.setValue(settingsKey("lastInstalledAt"), QDateTime::currentDateTimeUtc());
    settings.sync();
}

bool UpdateService::wouldRepeatAFailedInstall(const QString& version) {
    if (version.isEmpty() || lastInstalledVersion() != version) {
        return false;
    }
    const QDateTime when = lastInstalledAt();
    if (!when.isValid()) {
        return false;
    }
    // Long enough that a genuine second attempt after a restart is allowed,
    // short enough that a person who has fixed whatever stopped it does not
    // have to wait a day.
    return when.secsTo(QDateTime::currentDateTimeUtc()) < kRepeatInstallGuardSeconds;
}

bool UpdateService::canInstallUpdates() {
    const UpdateSituation situation = situationForThisBuild();
    return situation.updaterEnabled && canVerifyUpdates() &&
           canReplaceItself(situation.installKind);
}

void UpdateService::overrideSituation(const UpdateSituation& situation) {
    situationOverride_ = situation;
}

bool UpdateService::feedSignatureIsGood(const QByteArray& document,
                                        const QByteArray& signature) const {
    const SignatureCheck check = verifyUpdateSignature(document, signature);
    if (check.verified) {
        qCInfo(logApp) << "update feed signed by key" << check.keyFingerprint;
    } else {
        qCWarning(logApp) << "update feed signature rejected:" << check.problem;
    }
    return check.verified;
}

QString UpdateService::stagingDirectory() const {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return QDir(base).filePath(QStringLiteral("updates"));
}

void UpdateService::finishCheck(UpdateDecision decision) {
    decision_ = std::move(decision);
    busy_ = false;

    QSettings settings;
    settings.setValue(settingsKey("lastChecked"), QDateTime::currentDateTimeUtc());
    settings.sync();

    qCInfo(logApp) << "update check:" << describe(decision_.action) << "-" << decision_.reason;
    emit checked(decision_);
}

void UpdateService::checkForUpdate() {
    if (busy_) {
        return;
    }
    busy_ = true;

    UpdateSituation situation = situationOverride_ ? *situationOverride_ : situationForThisBuild();
    situation.preference = preference();
    if (!situation.now.isValid()) {
        situation.now = QDateTime::currentDateTimeUtc();
    }

    if (!situation.updaterEnabled) {
        UpdateDecision decision;
        decision.action = UpdateAction::CannotCheck;
        decision.reason = QStringLiteral("this build has no updater in it");
        finishCheck(std::move(decision));
        return;
    }

    const QUrl feed = feedUrl();
    if (!urlIsSafe(feed)) {
        UpdateDecision decision;
        decision.action = UpdateAction::CannotCheck;
        decision.reason = QStringLiteral(
                              "this build was given an update feed that is not a "
                              "permitted https address: %1")
                              .arg(feed.toString());
        finishCheck(std::move(decision));
        return;
    }

    const QUrl signatureUrl(feed.toString() + QStringLiteral(".sig"), QUrl::StrictMode);

    fetchDocument(
        feed, kFeedCap,
        [this, situation, signatureUrl](bool ok, QByteArray body, QString problem) mutable {
            if (!ok) {
                UpdateDecision decision;
                decision.action = UpdateAction::CannotCheck;
                decision.reason =
                    QStringLiteral("the update feed could not be read: %1").arg(problem);
                finishCheck(std::move(decision));
                return;
            }

            fetchDocument(
                signatureUrl, kSignatureCap,
                [this, situation, body](bool signedOk, QByteArray signature,
                                        QString signatureProblem) mutable {
                    // A missing signature is not an error in itself. It means nothing
                    // may be installed, which decideOnUpdate says far better than a
                    // failure here would.
                    if (signedOk) {
                        situation.feedVerified = feedSignatureIsGood(body, signature);
                        if (!situation.feedVerified) {
                            qCWarning(logApp) << "the update feed's signature was not accepted";
                        }
                    } else {
                        situation.feedVerified = false;
                        qCWarning(logApp) << "no update feed signature:" << signatureProblem;
                    }

                    const UpdateManifestReading reading = readUpdateManifest(body);
                    if (!reading.ok()) {
                        UpdateDecision decision;
                        decision.action = UpdateAction::CannotCheck;
                        decision.reason = reading.problem;
                        finishCheck(std::move(decision));
                        return;
                    }

                    finishCheck(decideOnUpdate(*reading.manifest, situation));
                });
        });
}

void UpdateService::downloadStagedUpdate() {
    if (busy_) {
        emit failed(QStringLiteral("a check is still running"));
        return;
    }
    if (!decision_.artifact) {
        emit failed(QStringLiteral("there is nothing staged to download"));
        return;
    }
    if (decision_.action != UpdateAction::Offer && decision_.action != UpdateAction::InstallNow) {
        emit failed(QStringLiteral("this copy is not allowed to install an update: %1")
                        .arg(decision_.reason));
        return;
    }

    const QString offered = decision_.release
                                ? QString::fromStdString(decision_.release->version.toString())
                                : QString();
    if (wouldRepeatAFailedInstall(offered)) {
        emit failed(QStringLiteral("%1 was installed here recently and this copy is still "
                                   "reporting %2, so it is not being downloaded again - "
                                   "something is putting the old version back")
                        .arg(offered, QLatin1String(TRANSMIT_VERSION)));
        return;
    }

    const UpdateArtifact artifact = *decision_.artifact;
    if (!urlIsSafe(artifact.url)) {
        emit failed(QStringLiteral("the release file is not at a permitted https address"));
        return;
    }
    if (artifact.size <= 0 || static_cast<qint64>(artifact.size) > kArtifactCap) {
        emit failed(QStringLiteral("the release file's declared size is not believable"));
        return;
    }

    const QString directory = stagingDirectory();
    if (!QDir().mkpath(directory)) {
        emit failed(QStringLiteral("could not make a place to download into: %1").arg(directory));
        return;
    }

    const QStorageInfo storage(directory);
    if (storage.isValid()) {
        const auto wanted =
            static_cast<qint64>(static_cast<double>(artifact.size) * kSpaceHeadroom);
        if (storage.bytesAvailable() < wanted) {
            emit failed(QStringLiteral("there is not enough room to download the update: it needs "
                                       "about %1 MB free and there is %2 MB")
                            .arg(wanted / (1024 * 1024))
                            .arg(storage.bytesAvailable() / (1024 * 1024)));
            return;
        }
    }

    // Named after the digest rather than the release, so a half-finished
    // download of one version can never be mistaken for another, and two
    // downloads of the same file land on the same name.
    const QString name = QStringLiteral("transmit-%1-%2")
                             .arg(QString::fromStdString(decision_.release->version.toString()),
                                  QString::fromLatin1(artifact.blake2b.toHex().left(16)));
    const QString target = QDir(directory).filePath(name);

    busy_ = true;
    fetchFile(
        artifact.url, target, static_cast<qint64>(artifact.size),
        [this, artifact, target](bool ok, QString problem) {
            busy_ = false;
            if (!ok) {
                QFile::remove(target);
                emit failed(problem);
                return;
            }

            QFile file(target);
            if (!file.open(QIODevice::ReadOnly)) {
                QFile::remove(target);
                emit failed(QStringLiteral("the download could not be read back to check it"));
                return;
            }

            const QFileInfo info(target);
            if (static_cast<quint64>(info.size()) != artifact.size) {
                QFile::remove(target);
                emit failed(QStringLiteral("the download is %1 bytes and the signed feed says %2")
                                .arg(info.size())
                                .arg(artifact.size));
                return;
            }

            // Read back from the disk rather than hashing what was written, so a
            // file that arrived intact and then failed to land intact is caught.
            format::Blake2b digest(32);
            while (!file.atEnd()) {
                const QByteArray chunk = file.read(1 << 20);
                if (chunk.isEmpty()) {
                    break;
                }
                digest.update({reinterpret_cast<const format::Byte*>(chunk.constData()),
                               static_cast<std::size_t>(chunk.size())});
            }
            file.close();

            const auto computed = digest.finish256();
            const QByteArray asBytes(reinterpret_cast<const char*>(computed.data()),
                                     static_cast<qsizetype>(computed.size()));
            if (asBytes != artifact.blake2b) {
                QFile::remove(target);
                emit failed(
                    QStringLiteral("the download does not match the digest in the signed "
                                   "feed, so it is not the file that was published"));
                return;
            }

            qCInfo(logApp) << "update staged at" << target;
            emit staged(target);
        });
}

}  // namespace transmit::core
