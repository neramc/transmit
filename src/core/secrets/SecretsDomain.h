#pragma once

#include <QList>
#include <QString>

#include "core/continuity/ContinuityTypes.h"
#include "format/Bytes.h"
#include "platform/PlatformService.h"

namespace transmit::core {

using platform::SecretKind;
using platform::SecretRecord;

/// Carries saved passwords between systems.
///
/// This is the one part of Transmit that handles material a user would be
/// harmed by losing control of, so it is deliberately hemmed in:
///
///   - it does nothing unless the user has explicitly asked for it
///   - the capture is refused outright unless the archive is encrypted
///   - the plaintext exists only in memory, and is overwritten as soon as it
///     has been handed on
///   - the report says clearly that the drive now contains passwords
///
/// It exists because the alternative is worse. Without it, a user who moves
/// systems loses every saved password at once and is told nothing about it
/// until they need one.
class SecretsDomain {
public:
    explicit SecretsDomain(const platform::PlatformService& platformService);

    struct CaptureOptions {
        bool includeWifi = true;
        bool includeApplications = true;
    };

    struct CaptureResult {
        format::ByteBuffer payload;
        int captured = 0;
        int unreadable = 0;   ///< found, but the system would not reveal the value
        QList<ContinuityNote> notes;
    };

    /// Reads what the user asked for. Never call this without an encrypted
    /// archive to put the result in.
    [[nodiscard]] CaptureResult capture(const CaptureOptions& options) const;

    /// Puts credentials into this system's store, reporting each outcome.
    [[nodiscard]] QList<ContinuityNote> restore(format::ByteView payload,
                                                const QString& scriptDirectory, bool dryRun) const;

    /// Whether this system can be read from or written to at all.
    [[nodiscard]] bool isAvailable() const;
    [[nodiscard]] QString describeStore() const;

private:
    const platform::PlatformService& platform_;
};

}  // namespace transmit::core
