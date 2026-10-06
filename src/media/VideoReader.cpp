#include "media/VideoReader.h"

#include <algorithm>

namespace lectern::media {

namespace {

constexpr Time kSeekAhead = Time::fromSeconds(2);  // forward jumps beyond this seek instead of decoding through
// Containers round timestamps (Matroska: 1 ms), so a frame due exactly at t
// may carry t + 0.5 ms. Requests at exact frame times (export) must still get it.
constexpr Time kTimestampSlack = Time::fromMilliseconds(1);

AVHWDeviceType preferredDevice() {
#if defined(__APPLE__)
    return AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
#elif defined(_WIN32)
    return AV_HWDEVICE_TYPE_D3D11VA;
#else
    return AV_HWDEVICE_TYPE_VAAPI;
#endif
}

}  // namespace

AVPixelFormat VideoReader::chooseFormat(AVCodecContext* ctx, const AVPixelFormat* formats) {
    const auto* self = static_cast<const VideoReader*>(ctx->opaque);
    for (const AVPixelFormat* f = formats; *f != AV_PIX_FMT_NONE; ++f) {
        if (self && *f == self->hwPixFmt_) return *f;
    }
    for (const AVPixelFormat* f = formats; *f != AV_PIX_FMT_NONE; ++f) {
        if (!isHardwarePixelFormat(*f)) return *f;
    }
    return formats[0];
}

Result<std::unique_ptr<VideoReader>> VideoReader::open(const std::filesystem::path& path,
                                                       const VideoReaderOptions& options) {
    std::unique_ptr<VideoReader> r(new VideoReader());
    AVFormatContext* raw = nullptr;
    if (const int ret = avformat_open_input(&raw, path.string().c_str(), nullptr, nullptr); ret < 0) {
        return fail(ffError(ret, "open " + path.filename().string(), ret == AVERROR(ENOENT) ? ErrorCode::NotFound
                                                                                          : ErrorCode::IoError));
    }
    r->format_.reset(raw);
    if (const int ret = avformat_find_stream_info(raw, nullptr); ret < 0) return fail(ffError(ret, "read stream info"));
    const AVCodec* codec = nullptr;
    r->stream_ = av_find_best_stream(raw, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (r->stream_ < 0 || !codec) return fail(ErrorCode::NotFound, path.filename().string() + " has no video");
    const AVStream* st = raw->streams[r->stream_];
    r->timeBase_ = st->time_base;

    r->codec_.reset(avcodec_alloc_context3(codec));
    if (!r->codec_) return fail(ErrorCode::OutOfMemory, "avcodec_alloc_context3");
    if (const int ret = avcodec_parameters_to_context(r->codec_.get(), st->codecpar); ret < 0) {
        return fail(ffError(ret, "decoder parameters"));
    }
    r->codec_->pkt_timebase = st->time_base;
    r->codec_->thread_count = options.threads;
    if (options.hardwareDecode) {
        const AVHWDeviceType type = preferredDevice();
        for (int i = 0;; ++i) {
            const AVCodecHWConfig* cfg = avcodec_get_hw_config(codec, i);
            if (!cfg) break;
            if ((cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && cfg->device_type == type) {
                AVBufferRef* device = nullptr;
                if (av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0) >= 0) {
                    r->hwDevice_ = BufferRef::adopt(device);
                    r->hwPixFmt_ = cfg->pix_fmt;
                    r->codec_->hw_device_ctx = r->hwDevice_.newRef();
                }
                break;
            }
        }
    }
    r->codec_->opaque = r.get();
    r->codec_->get_format = &VideoReader::chooseFormat;
    if (const int ret = avcodec_open2(r->codec_.get(), codec, nullptr); ret < 0) return fail(ffError(ret, "open decoder"));

    r->width_ = st->codecpar->width;
    r->height_ = st->codecpar->height;
    r->start_ = st->start_time != AV_NOPTS_VALUE ? Time::fromRational(st->start_time, Rational(st->time_base.num, st->time_base.den))
                                                 : Time::zero();
    if (st->duration > 0) {
        r->duration_ = Time::fromRational(st->duration, Rational(st->time_base.num, st->time_base.den));
    } else if (raw->duration > 0) {
        r->duration_ = Time::fromMicroseconds(raw->duration) - r->start_;
    }
    const AVRational fr = st->avg_frame_rate.num > 0 ? st->avg_frame_rate : st->r_frame_rate;
    if (fr.num > 0 && fr.den > 0) r->frameRate_ = FrameRate(Rational(fr.num, fr.den));
    return r;
}

VideoReader::~VideoReader() = default;

Time VideoReader::frameTime(const AVFrame* f) const {
    const std::int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    if (pts == AV_NOPTS_VALUE) return currentTime_ + frameRate_.frameDuration();
    return Time::fromRational(pts, Rational(timeBase_.num, timeBase_.den));
}

Status VideoReader::seek(Time t) {
    ++seeks_;
    const std::int64_t ts = std::max(t, start_).toRational(Rational(timeBase_.num, timeBase_.den), Rounding::Floor);
    if (const int ret = av_seek_frame(format_.get(), stream_, ts, AVSEEK_FLAG_BACKWARD); ret < 0) {
        // Some containers only seek by byte/any frame; restart from the beginning instead.
        if (const int r2 = av_seek_frame(format_.get(), stream_, 0, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY); r2 < 0) {
            return fail(ffError(ret, "seek"));
        }
    }
    avcodec_flush_buffers(codec_.get());
    current_.reset();
    next_.reset();
    eof_ = false;
    drained_ = false;
    return ok();
}

Result<Frame> VideoReader::decodeNext() {
    Frame frame = Frame::alloc();
    if (!frame) return fail(ErrorCode::OutOfMemory, "av_frame_alloc");
    for (;;) {
        const int ret = avcodec_receive_frame(codec_.get(), frame.get());
        if (ret == 0) break;
        if (ret == AVERROR_EOF) {
            drained_ = true;
            return Frame{};
        }
        if (ret != AVERROR(EAGAIN)) return fail(ffError(ret, "decode video"));
        if (eof_) {
            if (const int s = avcodec_send_packet(codec_.get(), nullptr); s < 0 && s != AVERROR_EOF) {
                return fail(ffError(s, "drain decoder"));
            }
            continue;
        }
        Packet pkt = Packet::alloc();
        if (!pkt) return fail(ErrorCode::OutOfMemory, "av_packet_alloc");
        const int r = av_read_frame(format_.get(), pkt.get());
        if (r == AVERROR_EOF) {
            eof_ = true;
            continue;
        }
        if (r < 0) return fail(ffError(r, "read video packet"));
        if (pkt->stream_index != stream_) continue;
        if (const int s = avcodec_send_packet(codec_.get(), pkt.get()); s < 0 && s != AVERROR(EAGAIN)) {
            if (s == AVERROR_INVALIDDATA) continue;  // damaged packet (e.g. salvaged file): skip it
            return fail(ffError(s, "send video packet"));
        }
    }
    ++decoded_;
    if (isHardwarePixelFormat(static_cast<AVPixelFormat>(frame->format))) {
        Frame sw = Frame::alloc();
        if (!sw) return fail(ErrorCode::OutOfMemory, "av_frame_alloc");
        if (const int ret = av_hwframe_transfer_data(sw.get(), frame.get(), 0); ret < 0) {
            return fail(ffError(ret, "download decoded frame"));
        }
        av_frame_copy_props(sw.get(), frame.get());
        return sw;
    }
    return frame;
}

Result<Frame> VideoReader::frameAt(Time t) {
    // Past the last frame: keep showing it without re-reading the file.
    const bool pastEnd = drained_ && current_ && t >= currentTime_;
    if (!pastEnd) {
        t += kTimestampSlack;
        const bool cached = current_ && t >= currentTime_ && next_ && t < nextTime_;
        const bool jump = !current_ || t < currentTime_ || (!cached && t > currentTime_ + kSeekAhead);
        if (jump) LEC_TRY(seek(t));
        for (;;) {
            if (!next_) {
                if (drained_) break;
                auto n = decodeNext();
                if (!n) return fail(std::move(n).error());
                if (!*n) break;  // end of stream
                nextTime_ = frameTime(n->get());
                next_ = std::move(*n);
            }
            if (nextTime_ > t && current_) break;
            current_ = std::move(next_);
            currentTime_ = nextTime_;
            next_.reset();
            if (currentTime_ > t) break;  // t precedes the first frame: show the earliest
        }
    }
    if (!current_) return fail(ErrorCode::NotFound, "no video frame at " + t.toString());
    Frame out = current_.ref();
    if (!out) return fail(ErrorCode::OutOfMemory, "ref frame");
    return out;
}

}  // namespace lectern::media
