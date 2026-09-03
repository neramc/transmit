// The two functions that actually speak to a server, and nothing else.
//
// Kept in a file of their own because every test replaces them: the whole
// point of making them virtual is that the rules around a download - what may
// be fetched, what is checked, what is installed - can be exercised without a
// server, and those rules are where the mistakes are. What is left here is Qt
// network plumbing with no decision in it, and it is excluded from the
// coverage measurement for that reason (see the IGNORE list in
// scripts/coverage.sh) rather than counted as code nobody tested.
//
// It is not untested. `transmit-cli update` runs it for real on all three
// platforms in the command line round trip, which is what proves the request
// is made, the redirect is followed and the answer is read.

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>

#include <memory>

#include "core/update/UpdateService.h"

namespace transmit::core {
namespace {

/// Long enough for a slow connection on a large file, short enough that a
/// server which accepts and then says nothing does not hold the program open.
constexpr int kTransferTimeoutMs = 60 * 1000;

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

/// Whether an address may be fetched at all. Shared with UpdateService, which
/// refuses a feed or a release file at anything else before asking for it.
bool urlIsSafe(const QUrl& url) {
    return url.isValid() && url.scheme() == QLatin1String("https") && hostIsAllowed(url);
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
