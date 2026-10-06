#pragma once

#include "core/Time.h"
#include "media/Frame.h"

#include <filesystem>
#include <memory>

namespace lectern::media {

struct VideoReaderOptions {
    /// VideoToolbox / D3D11VA / VA-API decoding when available (software
    /// fallback). Frames are always returned in system memory.
    bool hardwareDecode = true;
    int threads = 0;  ///< 0 = automatic
};

/// Random-access video frames by media time, tuned for forward playback:
/// requests that move forward decode on; jumps backwards (or more than two
/// seconds ahead) seek to the preceding keyframe first. One thread per reader.
class VideoReader {
public:
    static Result<std::unique_ptr<VideoReader>> open(const std::filesystem::path& path,
                                                     const VideoReaderOptions& options = {});
    ~VideoReader();
    VideoReader(const VideoReader&) = delete;
    VideoReader& operator=(const VideoReader&) = delete;

    /// The frame on screen at media time `t`: the last frame with pts ≤ t
    /// (the first frame for earlier times, the last one after the end).
    Result<Frame> frameAt(Time t);

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] Time start() const noexcept { return start_; }
    [[nodiscard]] Time duration() const noexcept { return duration_; }
    [[nodiscard]] FrameRate frameRate() const noexcept { return frameRate_; }
    [[nodiscard]] bool hardwareDecoding() const noexcept { return hwPixFmt_ != AV_PIX_FMT_NONE; }
    /// Frames decoded so far (seek cost diagnostics and tests).
    [[nodiscard]] std::uint64_t framesDecoded() const noexcept { return decoded_; }
    [[nodiscard]] std::uint64_t seeks() const noexcept { return seeks_; }

private:
    VideoReader() = default;
    static AVPixelFormat chooseFormat(AVCodecContext* ctx, const AVPixelFormat* formats);
    Status seek(Time t);
    /// Next frame in presentation order; empty Frame at end of stream.
    Result<Frame> decodeNext();
    [[nodiscard]] Time frameTime(const AVFrame* f) const;

    AVFormatInputPtr format_;
    AVCodecContextPtr codec_;
    BufferRef hwDevice_;
    AVPixelFormat hwPixFmt_ = AV_PIX_FMT_NONE;
    int stream_ = -1;
    AVRational timeBase_{1, 1};
    int width_ = 0;
    int height_ = 0;
    Time start_;
    Time duration_;
    FrameRate frameRate_{30, 1};
    Frame current_;  ///< last frame with pts ≤ the last request
    Frame next_;     ///< decoded lookahead (pts > the last request)
    Time currentTime_;
    Time nextTime_;
    bool eof_ = false;
    bool drained_ = false;
    std::uint64_t decoded_ = 0;
    std::uint64_t seeks_ = 0;
};

}  // namespace lectern::media
