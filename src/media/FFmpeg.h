#pragma once

// Single entry point for FFmpeg headers. Everything in the engine that needs
// libav* types includes this file, never the libav* headers directly.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "core/Error.h"
#include "core/Log.h"
#include "core/Rational.h"

#include <string>
#include <string_view>

// avcodec_get_supported_config() (FFmpeg 7.1) and the const AVIO write
// callback are required.
static_assert(LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100), "FFmpeg 7.1 or newer is required");

namespace lectern::media {

/// Human-readable text for an AVERROR code.
[[nodiscard]] std::string ffErrorString(int err);

/// Wraps an AVERROR into an Error. ENOSPC/EACCES/ENOENT are mapped to the
/// matching ErrorCode regardless of `code`, so disk-full is always visible.
[[nodiscard]] Error ffError(int err, std::string_view what, ErrorCode code = ErrorCode::MediaError);

[[nodiscard]] inline Rational fromAV(AVRational r) noexcept { return Rational(r.num, r.den); }
/// Narrowing conversion: the rational must fit in int (true for every time
/// base and frame rate we produce).
[[nodiscard]] inline AVRational toAV(Rational r) noexcept {
    return AVRational{static_cast<int>(r.num()), static_cast<int>(r.den())};
}

/// Routes FFmpeg's log output into lectern::Logger (category "ffmpeg").
/// Idempotent; call once at startup.
void initializeFFmpeg(LogLevel minimumLevel = LogLevel::Warn);

[[nodiscard]] bool isHardwarePixelFormat(AVPixelFormat fmt) noexcept;
[[nodiscard]] std::string pixelFormatName(AVPixelFormat fmt);
[[nodiscard]] std::string sampleFormatName(AVSampleFormat fmt);

}  // namespace lectern::media
