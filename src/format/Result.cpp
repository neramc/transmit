#include "format/Result.h"

namespace transmit::format {

std::string_view describe(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::None:
            return "no error";
        case ErrorCode::InvalidArgument:
            return "invalid argument";
        case ErrorCode::NotFound:
            return "not found";
        case ErrorCode::PermissionDenied:
            return "permission denied";
        case ErrorCode::IoError:
            return "input/output error";
        case ErrorCode::EndOfStream:
            return "unexpected end of stream";
        case ErrorCode::CorruptArchive:
            return "archive is corrupt";
        case ErrorCode::UnsupportedVersion:
            return "unsupported archive version";
        case ErrorCode::UnsupportedCodec:
            return "unsupported compression codec";
        case ErrorCode::CodecFailure:
            return "compression codec failure";
        case ErrorCode::IntegrityMismatch:
            return "integrity check failed";
        case ErrorCode::EncryptionUnavailable:
            return "this build has no encryption support";
        case ErrorCode::WrongPassphrase:
            return "wrong passphrase";
        case ErrorCode::VolumeMissing:
            return "an archive volume is missing";
        case ErrorCode::VolumeOutOfOrder:
            return "archive volumes are out of order";
        case ErrorCode::OutOfMemory:
            return "out of memory";
        case ErrorCode::Cancelled:
            return "cancelled";
        case ErrorCode::Internal:
            return "internal error";
    }
    return "unknown error";
}

std::string Error::toString() const {
    std::string text(describe(code));
    if (!message.empty() && message != text) {
        text += ": ";
        text += message;
    }
    return text;
}

}  // namespace transmit::format
