#pragma once

#include "core/Error.h"
#include "media/Frame.h"

#include <cstdint>
#include <span>

namespace lectern::media {

/// Decodes single JPEG images, the format a browser's `HTMLCanvasElement`
/// produces. Used by the phone camera path (docs/PHONE_CAMERA_PROTOCOL.md §8),
/// where the phone uploads preview frames as JPEG over HTTP.
///
/// Not thread safe: give each capture thread its own instance.
class JpegDecoder {
public:
    JpegDecoder();
    ~JpegDecoder();
    JpegDecoder(const JpegDecoder&) = delete;
    JpegDecoder& operator=(const JpegDecoder&) = delete;

    /// Decodes `data` into `format` (defaults to planar YUV 4:2:0 8-bit, which
    /// every encoder in the project accepts). Returns MediaError on a truncated
    /// or non-JPEG buffer.
    [[nodiscard]] Result<Frame> decode(std::span<const std::uint8_t> data, AVPixelFormat format = AV_PIX_FMT_YUV420P);

    /// Dimensions of the most recent successful decode, 0 before the first.
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }

private:
    Result<Frame> decodeNative(std::span<const std::uint8_t> data);

    AVCodecContextPtr codec_;
    SwsContextPtr scaler_;
    AVPixelFormat scalerFrom_ = AV_PIX_FMT_NONE;
    AVPixelFormat scalerTo_ = AV_PIX_FMT_NONE;
    int scalerWidth_ = 0;
    int scalerHeight_ = 0;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace lectern::media