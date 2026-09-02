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

/// Long enough for a slow connection on a large file, short enough that a
/// server which accepts and then says nothing does not hold the program open.
constexpr int kTransferTimeoutMs = 60 * 1000;

/// Free space wanted before a download starts: the file, and the same again,
/// because applying it writes a second copy before removing the first.
constexpr double kSpaceHeadroom = 2.2;

QString settingsKey(const char* leaf) {
    return QStringLiteral("updates/") + QString::fromLatin1(leaf);
}

/// Hosts a download may end up on. Redirects are followed - GitHub answers a
/// release download with one - but only onto this list, so a redirect cannot
/// walk the download somewhere else.
bool hostIsAllowed(const QUrl& url) {
    static const QStringList allowed = {
        QStringLiteral("github.com"),
        QStringLiteral("api.github.com"),
        QStringLiteral("objects.githubusercontent.com"),
        QStringLiteral("release-assets.githubusercontent.com"),
        QStringLiteral("raw.githubusercontent.com"),
    };
    const QString host = url.host().toLower();
    if (allowed.contains(host)) {
        return true;
    }
    // Whatever the feed itself was published on stays allowed, so a fork that
    // points TRANSMIT_UPDATE_FEED at its own server still works.
    const QString feedHost = UpdateService::feedUrl().host().toLower();
    return !feedHost.isEmpty() && host == feedHost;
}

bool urlIsSafe(const QUrl& url) {
    return url.isValid() && url.scheme() == QLatin1String("https") && hostIsAllowed(url);
}

QNetworkRequest requestFor(const QUrl& url) {
    QNetworkRequest request(url);
    request.setTransferTimeout(kTransferTimeoutMs);
    // Never follow a redirect that would drop to plain http.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QVariant::fromValue(QNetworkRequest::NoLessSafeRedirectPolicy));
    request.setMaximumRedirectsAllowed(5);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QVariant::fromValue(QNetworkRequest::AlwaysNetwork));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Transmit/%1").arg(QLatin1String(TRANSMIT_VERSION)));
    return request;
}

}  // namespace

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

void UpdateService::fetchDocument(const QUrl& url, qint64 cap,
                                  std::function<void(bool, QByteArray, QString)> done) {
    if (network_ == nullptr) {
        network_ = new QNetworkAccessManager(this);
    }

    QNetworkReply* const reply = network_->get(requestFor(url));
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, cap](qint64 received, qint64) {
        if (received > cap) {
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [reply, cap, done = std::move(done)]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done(false, {}, reply->errorString());
            return;
        }
        if (!urlIsSafe(reply->url())) {
            done(false, {},
                 QStringLiteral("the request was redirected somewhere unexpected: %1")
                     .arg(reply->url().toString()));
            return;
        }
        const QByteArray body = reply->readAll();
        if (body.size() > cap) {
            done(false, {}, QStringLiteral("the answer was larger than it could be"));
            return;
        }
        done(true, body, {});
    });
}

void UpdateService::fetchFile(const QUrl& url, const QString& target, qint64 cap,
                              std::function<void(bool, QString)> done) {
    if (network_ == nullptr) {
        network_ = new QNetworkAccessManager(this);
    }

    auto* const file = new QSaveFile(target);
    if (!file->open(QIODevice::WriteOnly)) {
        const QString problem = QStringLiteral("could not write to %1").arg(target);
        delete file;
        done(false, problem);
        return;
    }

    QNetworkReply* const reply = network_->get(requestFor(url));
    auto written = std::make_shared<qint64>(0);

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, reply, cap](qint64 received, qint64 total) {
                if (received > cap) {
                    reply->abort();
                    return;
                }
                emit progress(received, total);
            });

    connect(reply, &QNetworkReply::readyRead, reply, [reply, file, written, cap]() {
        const QByteArray chunk = reply->readAll();
        *written += chunk.size();
        if (*written > cap) {
            reply->abort();
            return;
        }
        if (file->write(chunk) != chunk.size()) {
            reply->abort();
        }
    });

    connect(reply, &QNetworkReply::finished, this,
            [reply, file, cap, written, done = std::move(done)]() {
                reply->deleteLater();
                std::unique_ptr<QSaveFile> owned(file);

                if (reply->error() != QNetworkReply::NoError) {
                    owned->cancelWriting();
                    done(false, reply->errorString());
                    return;
                }
                if (!urlIsSafe(reply->url())) {
                    owned->cancelWriting();
                    done(false, QStringLiteral("the download was redirected somewhere unexpected"));
                    return;
                }
                if (*written > cap) {
                    owned->cancelWriting();
                    done(false, QStringLiteral("the download was larger than the feed said"));
                    return;
                }
                if (!owned->commit()) {
                    done(false, QStringLiteral("the download could not be finished on disk: %1")
                                    .arg(owned->errorString()));
                    return;
                }
                done(true, {});
            });
}

}  // namespace transmit::core
