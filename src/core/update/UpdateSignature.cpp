#include "core/update/UpdateSignature.h"

#include <QStringList>

#include "format/hash/Blake2b.h"

#ifdef TRANSMIT_HAVE_OPENSSL
#include <openssl/evp.h>
#endif

namespace transmit::core {
namespace {

/// Ed25519: 32-byte public keys, 64-byte signatures. Both are fixed, and both
/// are checked before anything is handed to a library.
constexpr int kPublicKeySize = 32;
constexpr int kSignatureSize = 64;

/// A signature file is one short line. Anything larger is not one.
constexpr int kLargestSignatureFile = 512;

QString fingerprint(const QByteArray& key) {
    const format::ByteView view{reinterpret_cast<const format::Byte*>(key.constData()),
                                static_cast<std::size_t>(key.size())};
    return QString::fromStdString(format::Blake2b::hex256(view)).left(16);
}

}  // namespace

QList<QByteArray> trustedUpdateKeys() {
    QList<QByteArray> keys;
#ifdef TRANSMIT_UPDATE_KEYS
    const QString compiled = QString::fromLatin1(TRANSMIT_UPDATE_KEYS);
    for (const QString& encoded : compiled.split(u';', Qt::SkipEmptyParts)) {
        const QByteArray raw = QByteArray::fromBase64(encoded.trimmed().toLatin1(),
                                                      QByteArray::AbortOnBase64DecodingErrors);
        // A key the build was given but that is not a key is dropped rather
        // than trusted. Trusting 31 bytes of something would mean trusting
        // whatever OpenSSL made of the rest.
        if (raw.size() == kPublicKeySize) {
            keys.append(raw);
        }
    }
#endif
    return keys;
}

bool canVerifyUpdates() {
#ifdef TRANSMIT_HAVE_OPENSSL
    return !trustedUpdateKeys().isEmpty();
#else
    return false;
#endif
}

std::optional<QByteArray> readDetachedSignature(const QByteArray& text) {
    if (text.isEmpty() || text.size() > kLargestSignatureFile) {
        return std::nullopt;
    }
    const QByteArray trimmed = text.trimmed();
    const QByteArray::FromBase64Result decoded =
        QByteArray::fromBase64Encoding(trimmed, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded) {
        return std::nullopt;
    }
    if (decoded.decoded.size() != kSignatureSize) {
        return std::nullopt;
    }
    return decoded.decoded;
}

SignatureCheck verifyDetachedSignature(const QByteArray& document, const QByteArray& signature,
                                       const QList<QByteArray>& keys) {
    SignatureCheck check;

    if (document.isEmpty()) {
        check.problem = QStringLiteral("there was nothing to check the signature against");
        return check;
    }
    if (signature.size() != kSignatureSize) {
        check.problem = QStringLiteral("the signature is %1 bytes rather than %2")
                            .arg(signature.size())
                            .arg(kSignatureSize);
        return check;
    }
    if (keys.isEmpty()) {
        check.problem = QStringLiteral("this build trusts no update signing keys");
        return check;
    }

#ifndef TRANSMIT_HAVE_OPENSSL
    check.problem = QStringLiteral("this build has no OpenSSL, so it cannot check a signature");
    return check;
#else
    for (const QByteArray& key : keys) {
        if (key.size() != kPublicKeySize) {
            continue;
        }

        EVP_PKEY* const publicKey = EVP_PKEY_new_raw_public_key(
            EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char*>(key.constData()),
            static_cast<std::size_t>(key.size()));
        if (publicKey == nullptr) {
            continue;
        }

        EVP_MD_CTX* const context = EVP_MD_CTX_new();
        if (context == nullptr) {
            EVP_PKEY_free(publicKey);
            continue;
        }

        int verified = 0;
        if (EVP_DigestVerifyInit(context, nullptr, nullptr, nullptr, publicKey) == 1) {
            verified = EVP_DigestVerify(
                context, reinterpret_cast<const unsigned char*>(signature.constData()),
                static_cast<std::size_t>(signature.size()),
                reinterpret_cast<const unsigned char*>(document.constData()),
                static_cast<std::size_t>(document.size()));
        }

        EVP_MD_CTX_free(context);
        EVP_PKEY_free(publicKey);

        // Exactly 1 means verified. OpenSSL returns 0 for a bad signature and
        // a negative number for an error, and treating "not 0" as success is
        // the classic way to accept everything.
        if (verified == 1) {
            check.verified = true;
            check.keyFingerprint = fingerprint(key);
            return check;
        }
    }

    check.problem = QStringLiteral("the signature does not match any key this build trusts");
    return check;
#endif
}

SignatureCheck verifyUpdateSignature(const QByteArray& document, const QByteArray& signatureFile) {
    const auto signature = readDetachedSignature(signatureFile);
    if (!signature) {
        SignatureCheck check;
        check.problem =
            QStringLiteral("the signature file is not %1 base64 bytes").arg(kSignatureSize);
        return check;
    }
    return verifyDetachedSignature(document, *signature, trustedUpdateKeys());
}

}  // namespace transmit::core
