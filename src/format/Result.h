#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace transmit::format {

/// Every failure inside the format layer is reported through Result rather than
/// an exception, so the layer can be linked into environments (and callers)
/// that disable exceptions.
enum class ErrorCode {
    None = 0,
    InvalidArgument,
    NotFound,
    PermissionDenied,
    IoError,
    EndOfStream,
    CorruptArchive,
    UnsupportedVersion,
    UnsupportedCodec,
    CodecFailure,
    IntegrityMismatch,
    EncryptionUnavailable,
    WrongPassphrase,
    VolumeMissing,
    VolumeOutOfOrder,
    OutOfMemory,
    Cancelled,
    Internal,
};

std::string_view describe(ErrorCode code) noexcept;

struct Error {
    ErrorCode code = ErrorCode::Internal;
    std::string message;

    Error() = default;
    Error(ErrorCode c, std::string msg) : code(c), message(std::move(msg)) {}
    explicit Error(ErrorCode c) : code(c), message(describe(c)) {}

    [[nodiscard]] std::string toString() const;
};

/// Minimal expected-like type. `Result<void>` is supported through a dedicated
/// specialisation so functions that only report failure share the same shape.
template <typename T>
class [[nodiscard]] Result {
public:
    using value_type = T;

    Result(T value) : storage_(std::move(value)) {}      // NOLINT(google-explicit-constructor)
    Result(Error error) : storage_(std::move(error)) {}  // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool hasValue() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return hasValue(); }

    T& value() & { return std::get<0>(storage_); }
    const T& value() const& { return std::get<0>(storage_); }
    T&& value() && { return std::get<0>(std::move(storage_)); }

    T* operator->() { return &std::get<0>(storage_); }
    const T* operator->() const { return &std::get<0>(storage_); }
    T& operator*() & { return std::get<0>(storage_); }
    const T& operator*() const& { return std::get<0>(storage_); }

    [[nodiscard]] const Error& error() const& { return std::get<1>(storage_); }
    [[nodiscard]] Error&& error() && { return std::get<1>(std::move(storage_)); }

    [[nodiscard]] T valueOr(T fallback) const& {
        return hasValue() ? std::get<0>(storage_) : std::move(fallback);
    }

private:
    std::variant<T, Error> storage_;
};

template <>
class [[nodiscard]] Result<void> {
public:
    using value_type = void;

    Result() = default;
    Result(Error error) : error_(std::move(error)) {}  // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool hasValue() const noexcept { return !error_.has_value(); }
    explicit operator bool() const noexcept { return hasValue(); }

    void value() const {}

    [[nodiscard]] const Error& error() const& { return *error_; }
    [[nodiscard]] Error&& error() && { return std::move(*error_); }

private:
    std::optional<Error> error_;
};

using Status = Result<void>;

inline Status ok() { return {}; }

template <typename... Args>
Error makeError(ErrorCode code, Args&&... parts) {
    std::string message;
    (message.append(std::forward<Args>(parts)), ...);
    if (message.empty()) {
        message = std::string(describe(code));
    }
    return Error{code, std::move(message)};
}

/// Propagates a failure from an inner Result, converting the value type.
#define TRANSMIT_TRY(target, expr)                       \
    auto&& target##_result_ = (expr);                    \
    if (!target##_result_) {                             \
        return std::move(target##_result_).error();      \
    }                                                    \
    auto target = std::move(target##_result_).value()

#define TRANSMIT_CHECK(expr)                             \
    do {                                                 \
        auto&& check_result_ = (expr);                   \
        if (!check_result_) {                            \
            return std::move(check_result_).error();     \
        }                                                \
    } while (false)

}  // namespace transmit::format
