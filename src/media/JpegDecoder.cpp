#include "media/JpegDecoder.h"

#include <cstring>
#include <vector>

namespace lectern::media {
namespace {

/// The YUVJ formats are deprecated aliases of their plain twins and have the
/// same layout; the only difference is the range, which travels in
/// AVFrame::color_range instead.
AVPixelFormat withoutJpegRange(AVPixelFormat format) noexcept {
    switch (format) {
        case AV_PIX_FMT_YUVJ420P: return AV_PIX_FMT_YUV420P;
        case AV_PIX_FMT_YUVJ422P: return AV_PIX_FMT_YUV422P;
        case AV_PIX_FMT_YUVJ444P: return AV_PIX_FMT_YUV444P;
        case AV_PIX_FMT_YUVJ440P: return AV_PIX_FMT_YUV440P;
        case AV_PIX_FMT_YUVJ411P: return AV_PIX_FMT_YUV411P;
        default: return format;
    }
}

}  // namespace

JpegDecoder::JpegDecoder() {
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
    if (!codec) return;
    codec_ = AVCodecContextPtr(avcodec_alloc_context3(codec));
    if (codec_) avcodec_open2(codec_.get(), codec, nullptr);
}

JpegDecoder::~JpegDecoder() = default;

Result<Frame> JpegDecoder::decodeNative(std::span<const std::uint8_t> data) {
    if (!codec_) return fail(ErrorCode::Unsupported, "no mjpeg decoder available");
    if (data.empty()) return fail(ErrorCode::InvalidArgument, "empty jpeg buffer");

    Packet packet = Packet::alloc();
    if (!packet) return fail(ErrorCode::OutOfMemory, "allocating packet");
    // FFmpeg's decoder takes a const buffer but writes padding into it, so hand
    // it a copy with AV_INPUT_BUFFER_PADDING_SIZE slack.
    constexpr std::size_t kPadding = AV_INPUT_BUFFER_PADDING_SIZE;
    std::vector<std::uint8_t> padded(data.size() + kPadding, 0);
    std::memcpy(padded.data(), data.data(), data.size());
    packet->data = padded.data();
    packet->size = static_cast<int>(data.size());

    int rc = avcodec_send_packet(codec_.get(), packet.get());
    if (rc < 0) {
        std::string detail(64, '\0');
        av_strerror(rc, detail.data(), detail.size());
        return fail(ErrorCode::MediaError, "jpeg: " + std::string(detail.data()));
    }

    Frame frame = Frame::alloc();
    if (!frame) return fail(ErrorCode::OutOfMemory, "allocating frame");
    rc = avcodec_receive_frame(codec_.get(), frame.get());
    if (rc < 0) {
        std::string detail(64, '\0');
        av_strerror(rc, detail.data(), detail.size());
        return fail(ErrorCode::MediaError, "jpeg: " + std::string(detail.data()));
    }
    width_ = frame->width;
    height_ = frame->height;
    return frame;
}

Result<Frame> JpegDecoder::decode(std::span<const std::uint8_t> data, AVPixelFormat format) {
    auto decoded = decodeNative(data);
    if (!decoded) return fail(Error(std::move(decoded).error()));
    Frame frame = std::move(decoded).value();

    // The decoder may hand back a format other than the one we assume (e.g.
    // greyscale for a low-detail frame); normalize before scaling.
    const auto actual = static_cast<AVPixelFormat>(frame->format);
    if (actual == format) return frame;

    // Feed swscale the non-deprecated twin of the format and state the range
    // ourselves: handing it YUVJ420P makes it warn on every single frame, and
    // the two have identical memory layout.
    const AVPixelFormat sourceFormat = withoutJpegRange(actual);
    const int srcRange = frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
    const int dstRange = srcRange;

    if (!scaler_ || scalerFrom_ != sourceFormat || scalerTo_ != format || scalerWidth_ != frame->width ||
        scalerHeight_ != frame->height) {
        scaler_ = SwsContextPtr(sws_getContext(frame->width, frame->height, sourceFormat, frame->width, frame->height,
                                               format, SWS_BILINEAR, nullptr, nullptr, nullptr));
        if (!scaler_) return fail(ErrorCode::MediaError, "sws_getContext for jpeg");
        const int* coefficients = sws_getCoefficients(SWS_CS_ITU601);
        sws_setColorspaceDetails(scaler_.get(), coefficients, srcRange, coefficients, dstRange, 0, 0, 0);
        scalerFrom_ = sourceFormat;
        scalerTo_ = format;
        scalerWidth_ = frame->width;
        scalerHeight_ = frame->height;
    }

    auto converted = Frame::allocVideo(frame->width, frame->height, format);
    if (!converted) return fail(Error(std::move(converted).error()));
    sws_scale(scaler_.get(), frame->data, frame->linesize, 0, frame->height, (*converted)->data,
              (*converted)->linesize);
    (*converted)->color_range = frame->color_range;
    return converted;
}

}  // namespace lectern::media