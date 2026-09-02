#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <optional>

#include "core/update/UpdateDecision.h"

class QNetworkAccessManager;

namespace transmit::core {

/// Finds out whether there is a newer Transmit, and fetches it.
///
/// The network is behind two virtual functions so the whole of this class can
/// be driven from a test with canned answers. The interesting cases - a feed
/// that is signed by the wrong key, a download that is one byte short, a
/// critical release arriving at a Flatpak - are the ones that are hardest to
/// arrange against a real server and the ones most worth being sure about.
class UpdateService : public QObject {
    Q_OBJECT

public:
    explicit UpdateService(QObject* parent = nullptr);
    ~UpdateService() override;

    /// Where the feed is published, and where people are sent to fetch a
    /// release by hand. Both are compiled in: a file next to the program could
    /// be edited by whatever is trying to feed this machine an update.
    [[nodiscard]] static QUrl feedUrl();
    [[nodiscard]] static QUrl releasesPage();

    [[nodiscard]] static UpdatePreference preference();
    static void setPreference(UpdatePreference preference);

    /// When the last check happened, so a background check can be rare.
    [[nodiscard]] static QDateTime lastChecked();

    /// Whether this build could install anything at all, before asking the
    /// network. Used to keep the interface honest about what it is offering.
    [[nodiscard]] static bool canInstallUpdates();

    /// Fetches the feed and its signature, and works out what to do. Emits
    /// checked() exactly once, whatever happens.
    void checkForUpdate();

    /// Downloads the artifact from the last decision, checks it against the
    /// digest in the signed feed, and stages it. Emits staged() or failed()
    /// exactly once.
    void downloadStagedUpdate();

    [[nodiscard]] const UpdateDecision& lastDecision() const { return decision_; }

    /// Overrides what the machine looks like. For tests only; the real values
    /// come from situationForThisBuild().
    void overrideSituation(const UpdateSituation& situation);

signals:
    void checked(const transmit::core::UpdateDecision& decision);
    void progress(qint64 received, qint64 total);
    void staged(const QString& path);
    void failed(const QString& problem);

protected:
    /// Fetches a small document. `cap` is a hard byte limit: a server that
    /// keeps sending is cut off rather than filling memory.
    virtual void fetchDocument(const QUrl& url, qint64 cap,
                               std::function<void(bool, QByteArray, QString)> done);

    /// Fetches a file to `target`, reporting progress. The file is written
    /// through a temporary name and only moved into place once complete.
    virtual void fetchFile(const QUrl& url, const QString& target, qint64 cap,
                           std::function<void(bool, QString)> done);

    /// Where downloads are staged. Overridden in tests.
    [[nodiscard]] virtual QString stagingDirectory() const;

    /// Whether the feed was signed by a key this build trusts.
    ///
    /// Virtual so a test can answer both ways without holding a signing key.
    /// The answer decides whether anything is downloaded at all, so the two
    /// answers are the two halves of this feature and both need exercising;
    /// the signature check itself is tested against RFC 8032's own vectors.
    [[nodiscard]] virtual bool feedSignatureIsGood(const QByteArray& document,
                                                   const QByteArray& signature) const;

private:
    void finishCheck(UpdateDecision decision);

    QNetworkAccessManager* network_ = nullptr;
    UpdateDecision decision_;
    std::optional<UpdateSituation> situationOverride_;
    bool busy_ = false;
};

}  // namespace transmit::core
