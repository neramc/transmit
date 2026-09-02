#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace transmit::core {

/// What came of checking a detached signature.
struct SignatureCheck {
    bool verified = false;
    QString problem;

    /// The first eight bytes of the matching key's BLAKE2b digest, in hex, so
    /// a log can say which key signed something without printing the key.
    QString keyFingerprint;
};

/// The public keys this build was compiled to trust, as raw 32-byte Ed25519
/// keys. Empty when the build was given none, which is the default: a project
/// that has not set up signing gets an updater that will tell people a new
/// version exists and will not download or install anything.
[[nodiscard]] QList<QByteArray> trustedUpdateKeys();

/// Whether this build can check a signature at all. False without OpenSSL, and
/// false with no trusted keys. Both answers mean the same thing to the caller:
/// nothing may be installed.
[[nodiscard]] bool canVerifyUpdates();

/// Reads a detached signature file: base64 of the 64 raw signature bytes, with
/// whitespace and a trailing newline allowed and nothing else.
[[nodiscard]] std::optional<QByteArray> readDetachedSignature(const QByteArray& text);

/// Checks `signature` against the exact bytes of `document` using every trusted
/// key in turn, so a key can be rotated by publishing feeds signed with the new
/// one while old builds still accept the old.
///
/// Fails closed in every direction: no OpenSSL, no keys, a malformed signature,
/// a signature that verifies against none of the keys - all the same answer.
[[nodiscard]] SignatureCheck verifyDetachedSignature(const QByteArray& document,
                                                     const QByteArray& signature,
                                                     const QList<QByteArray>& keys);

/// The same, against the compiled-in keys.
[[nodiscard]] SignatureCheck verifyUpdateSignature(const QByteArray& document,
                                                   const QByteArray& signatureFile);

}  // namespace transmit::core
