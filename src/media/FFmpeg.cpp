#include "media/FFmpeg.h"

#include <cerrno>
#include <cstdio>
#include <mutex>

namespace lectern::media {

std::string ffErrorString(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof buf);
    return buf;
}

Error ffError(int err, std::string_view what, ErrorCode code) {
    if (err == AVERROR(ENOSPC)) code = ErrorCode::OutOfSpace;
#ifdef EDQUOT
    else if (err == AVERROR(EDQUOT)) code = ErrorCode::OutOfSpace;
#endif
    else if (err == AVERROR(EACCES) || err == AVERROR(EPERM)) code = ErrorCode::PermissionDenied;
    else if (err == AVERROR(ENOENT)) code = ErrorCode::NotFound;
    else if (err == AVERROR(ENOMEM)) code = ErrorCode::OutOfMemory;
    else if (err == AVERROR_INVALIDDATA) code = code == ErrorCode::MediaError ? ErrorCode::Corrupt : code;
    std::string msg(what);
    msg += ": ";
    msg += ffErrorString(err);
    return Error(code, std::move(msg), err);
}

namespace {

LogLevel mapLevel(int avLevel) {
    if (avLevel <= AV_LOG_FATAL) return LogLevel::Critical;
    if (avLevel <= AV_LOG_ERROR) return LogLevel::Error;
    if (avLevel <= AV_LOG_WARNING) return LogLevel::Warn;
    if (avLevel <= AV_LOG_INFO) return LogLevel::Info;
    if (avLevel <= AV_LOG_VERBOSE) return LogLevel::Debug;
    return LogLevel::Trace;
}

int toAvLevel(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return AV_LOG_TRACE;
        case LogLevel::Debug: return AV_LOG_VERBOSE;
        case LogLevel::Info: return AV_LOG_INFO;
        case LogLevel::Warn: return AV_LOG_WARNING;
        case LogLevel::Error: return AV_LOG_ERROR;
        case LogLevel::Critical: return AV_LOG_FATAL;
        case LogLevel::Off: return AV_LOG_QUIET;
    }
    return AV_LOG_WARNING;
}

void logCallback(void* avcl, int level, const char* fmt, va_list args) {
    if (level > av_log_get_level()) return;
    const LogLevel ours = mapLevel(level);
    if (!Logger::instance().shouldLog(ours)) return;

    // FFmpeg emits partial lines; buffer per thread until a newline arrives.
    thread_local std::string pending;
    thread_local int printPrefix = 1;
    char line[1024];
    av_log_format_line2(avcl, level, fmt, args, line, sizeof line, &printPrefix);
    pending += line;
    if (pending.empty() || pending.back() != '\n') return;
    pending.pop_back();
    Logger::instance().log(ours, "ffmpeg", std::move(pending), nullptr, 0);
    pending.clear();
}

}  // namespace

void initializeFFmpeg(LogLevel minimumLevel) {
    static std::once_flag once;
    std::call_once(once, [] { av_log_set_callback(&logCallback); });
    av_log_set_level(toAvLevel(minimumLevel));
}

bool isHardwarePixelFormat(AVPixelFormat fmt) noexcept {
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(fmt);
    return desc && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL);
}

std::string pixelFormatName(AVPixelFormat fmt) {
    const char* name = av_get_pix_fmt_name(fmt);
    return name ? name : "none";
}

std::string sampleFormatName(AVSampleFormat fmt) {
    const char* name = av_get_sample_fmt_name(fmt);
    return name ? name : "none";
}

}  // namespace lectern::media
