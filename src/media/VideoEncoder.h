#pragma once

#include "core/Time.h"
#include "media/Frame.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lectern::media {

enum class VideoCodec { H264, HEVC };
enum class EncoderPreference {
    Auto,          ///< hardware encoders first, software fallback
    HardwareOnly,  ///< fail if no hardware encoder opens
    SoftwareOnly,  ///< deterministic output (tests), or user choice
};

[[nodiscard]] std::string_view toString(VideoCodec codec) noexcept;
[[nodiscard]] std::string_view toString(EncoderPreference preference) noexcept;
[[nodiscard]] EncoderPreference encoderPreferenceFromString(std::string_view text) noexcept;

struct ColorInfo {
    AVColorPrimaries primaries = AVCOL_PRI_BT709;
    AVColorTransferCharacteristic transfer = AVCOL_TRC_BT709;
    AVColorSpace space = AVCOL_SPC_BT709;
    AVColorRange range = AVCOL_RANGE_MPEG;
};

struct VideoEncoderConfig {
    VideoCodec codec = VideoCodec::H264;
    EncoderPreference preference = EncoderPreference::Auto;
    int width = 0;
    int height = 0;
    FrameRate frameRate{30, 1};
    std::int64_t bitRate = 8'000'000;
    int gopFrames = 60;
    bool realtime = true;      ///< live capture: favor throughput/latency
    bool globalHeader = true;  ///< codec config in container header (Matroska/MP4)
    ColorInfo color;

    /// Format of the frames that will be submitted (taken from the first
    /// captured frame). For hardware frames, `inputSwFormat` is the
    /// underlying layout and `inputHwFramesCtx` the frames context (borrowed).
    AVPixelFormat inputFormat = AV_PIX_FMT_NV12;
    AVPixelFormat inputSwFormat = AV_PIX_FMT_NONE;
    const AVBufferRef* inputHwFramesCtx = nullptr;

    /// Explicit encoder names to try in order (overrides preference lists).
    std::vector<std::string> encoderNames;
};

struct EncoderIdentity {
    std::string name;
    bool hardware = false;
    bool zeroCopy = false;  ///< hardware frames are passed straight through
    AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;
};

using PacketSink = std::function<Status(Packet&&)>;

/// Encoder candidates for this platform in preference order
/// (docs/ARCHITECTURE.md §9).
[[nodiscard]] std::vector<std::string> videoEncoderCandidates(VideoCodec codec, EncoderPreference preference);
[[nodiscard]] bool isHardwareEncoder(const AVCodec& codec) noexcept;

/// FFmpeg-backed video encoder with hardware selection and software fallback.
///
/// Input frames may be software frames in any format or hardware frames.
/// The encoder adapts them with the fewest copies possible:
///   hardware frame + encoder accepts it   → passthrough (zero-copy)
///   hardware frame + software encoder     → one download
///   software frame + format mismatch      → one swscale conversion
///   software frame + hardware-only input  → one upload (VA-API style)
class VideoEncoder {
public:
    static Result<std::unique_ptr<VideoEncoder>> create(const VideoEncoderConfig& config);
    ~VideoEncoder();
    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    /// Encodes one frame. `frame->pts` must be in timeBase() units.
    Status encode(Frame frame, bool forceKeyframe, const PacketSink& sink);
    /// Drains the encoder; afterwards no more frames may be submitted.
    Status flush(const PacketSink& sink);

    [[nodiscard]] const AVCodecContext& context() const noexcept { return *ctx_; }
    [[nodiscard]] AVRational timeBase() const noexcept { return ctx_->time_base; }
    [[nodiscard]] const EncoderIdentity& identity() const noexcept { return identity_; }
    [[nodiscard]] std::int64_t framesEncoded() const noexcept { return framesIn_; }

private:
    VideoEncoder() = default;
    Result<Frame> adapt(Frame frame);
    Status drain(const PacketSink& sink);

    AVCodecContextPtr ctx_;
    EncoderIdentity identity_;
    VideoEncoderConfig config_;
    SwsContextPtr sws_;
    Frame convertScratch_;
    BufferRef uploadFramesCtx_;
    std::int64_t framesIn_ = 0;
    bool flushed_ = false;
};

}  // namespace lectern::media
