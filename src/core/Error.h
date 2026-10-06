#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace lectern {

/// Coarse error categories. The UI maps these to user-facing messages; the
/// message/context chain carries the technical detail for logs.
enum class ErrorCode : std::uint16_t {
    Unknown = 1,
    InvalidArgument,
    InvalidState,
    NotFound,
    AlreadyExists,
    PermissionDenied,
    Unsupported,
    IoError,
    OutOfSpace,
    Corrupt,
    Timeout,
    Cancelled,
    DeviceLost,
    DeviceBusy,
    MediaError,
    EncoderError,
    DecoderError,
    NetworkError,
    ParseError,
    OutOfMemory,
    Internal,
};

[[nodiscard]] std::string_view toString(ErrorCode code) noexcept;

class Error {
public:
    Error(ErrorCode code, std::string message, std::int64_t nativeCode = 0)
        : code_(code), message_(std::move(message)), nativeCode_(nativeCode) {}

    [[nodiscard]] ErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    /// errno / OSStatus / HRESULT / AVERROR, 0 if none.
    [[nodiscard]] std::int64_t nativeCode() const noexcept { return nativeCode_; }

    /// Prepends context: "opening screen track: <previous message>".
    Error& withContext(std::string_view context) & {
        message_ = std::string(context) + ": " + message_;
        return *this;
    }
    Error&& withContext(std::string_view context) && {
        message_ = std::string(context) + ": " + message_;
        return std::move(*this);
    }

    /// "[io-error] opening file: No such file or directory (native 2)"
    [[nodiscard]] std::string toString() const;

private:
    ErrorCode code_;
    std::string message_;
    std::int64_t nativeCode_;
};

template <class T>
using Result = std::expected<T, Error>;
using Status = std::expected<void, Error>;

[[nodiscard]] inline std::unexpected<Error> fail(ErrorCode code, std::string message, std::int64_t nativeCode = 0) {
    return std::unexpected<Error>(Error(code, std::move(message), nativeCode));
}
[[nodiscard]] inline std::unexpected<Error> fail(Error error) { return std::unexpected<Error>(std::move(error)); }
[[nodiscard]] inline Status ok() noexcept { return {}; }

/// Error from errno (or an explicit errno value) with strerror text appended.
[[nodiscard]] Error errnoError(ErrorCode code, std::string_view what, int err);
/// Maps errno to the closest ErrorCode (ENOSPC → OutOfSpace, EACCES → PermissionDenied, ...).
[[nodiscard]] ErrorCode errorCodeFromErrno(int err) noexcept;

}  // namespace lectern

/// Propagates an error from a Result/Status-returning expression.
#define LEC_TRY(expr)                                                         \
    do {                                                                      \
        auto&& lec_try_result_ = (expr);                                      \
        if (!lec_try_result_) [[unlikely]]                                    \
            return std::unexpected(std::move(lec_try_result_).error());       \
    } while (false)
