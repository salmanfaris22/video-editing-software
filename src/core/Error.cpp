#include "core/Error.h"

#include <cerrno>
#include <cstring>

namespace lectern {

std::string_view toString(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Unknown: return "unknown";
        case ErrorCode::InvalidArgument: return "invalid-argument";
        case ErrorCode::InvalidState: return "invalid-state";
        case ErrorCode::NotFound: return "not-found";
        case ErrorCode::AlreadyExists: return "already-exists";
        case ErrorCode::PermissionDenied: return "permission-denied";
        case ErrorCode::Unsupported: return "unsupported";
        case ErrorCode::IoError: return "io-error";
        case ErrorCode::OutOfSpace: return "out-of-space";
        case ErrorCode::Corrupt: return "corrupt";
        case ErrorCode::Timeout: return "timeout";
        case ErrorCode::Cancelled: return "cancelled";
        case ErrorCode::DeviceLost: return "device-lost";
        case ErrorCode::DeviceBusy: return "device-busy";
        case ErrorCode::MediaError: return "media-error";
        case ErrorCode::EncoderError: return "encoder-error";
        case ErrorCode::DecoderError: return "decoder-error";
        case ErrorCode::NetworkError: return "network-error";
        case ErrorCode::ParseError: return "parse-error";
        case ErrorCode::OutOfMemory: return "out-of-memory";
        case ErrorCode::Internal: return "internal";
    }
    return "unknown";
}

std::string Error::toString() const {
    std::string out;
    out.reserve(message_.size() + 32);
    out += '[';
    out += lectern::toString(code_);
    out += "] ";
    out += message_;
    if (nativeCode_ != 0) {
        out += " (native ";
        out += std::to_string(nativeCode_);
        out += ')';
    }
    return out;
}

ErrorCode errorCodeFromErrno(int err) noexcept {
    switch (err) {
        case ENOSPC:
#ifdef EDQUOT
        case EDQUOT:
#endif
            return ErrorCode::OutOfSpace;
        case EACCES:
        case EPERM:
        case EROFS:
            return ErrorCode::PermissionDenied;
        case ENOENT:
        case ENOTDIR:
            return ErrorCode::NotFound;
        case EEXIST:
            return ErrorCode::AlreadyExists;
        case EINVAL:
            return ErrorCode::InvalidArgument;
        case ENOMEM:
            return ErrorCode::OutOfMemory;
        case EBUSY:
            return ErrorCode::DeviceBusy;
        case ETIMEDOUT:
            return ErrorCode::Timeout;
        default:
            return ErrorCode::IoError;
    }
}

Error errnoError(ErrorCode code, std::string_view what, int err) {
    std::string msg(what);
    msg += ": ";
    msg += std::strerror(err);
    return Error(code, std::move(msg), err);
}

}  // namespace lectern
