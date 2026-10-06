#include "support/TestSupport.h"

#include "core/Uuid.h"
#include "media/Frame.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace lectern::test {

TempDir::TempDir(const std::string& prefix) {
    path_ = std::filesystem::temp_directory_path() / (prefix + "-" + Uuid::generateV4().toString().substr(0, 8));
    std::filesystem::create_directories(path_);
}

TempDir::~TempDir() {
    if (std::getenv("LECTERN_KEEP_TEST_FILES")) return;
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
}

namespace {

struct Decoder {
    media::AVFormatInputPtr fmt;
    media::AVCodecContextPtr ctx;
    int stream = -1;
};

Result<Decoder> openDecoder(const std::filesystem::path& path, AVMediaType type) {
    Decoder d;
    AVFormatContext* raw = nullptr;
    if (int ret = avformat_open_input(&raw, path.string().c_str(), nullptr, nullptr); ret < 0) {
        return fail(media::ffError(ret, "open " + path.string()));
    }
    d.fmt.reset(raw);
    if (int ret = avformat_find_stream_info(d.fmt.get(), nullptr); ret < 0) return fail(media::ffError(ret, "info"));
    const AVCodec* codec = nullptr;
    d.stream = av_find_best_stream(d.fmt.get(), type, -1, -1, &codec, 0);
    if (d.stream < 0 || !codec) return fail(ErrorCode::NotFound, "no stream of requested type");
    d.ctx.reset(avcodec_alloc_context3(codec));
    avcodec_parameters_to_context(d.ctx.get(), d.fmt->streams[d.stream]->codecpar);
    d.ctx->pkt_timebase = d.fmt->streams[d.stream]->time_base;
    if (int ret = avcodec_open2(d.ctx.get(), codec, nullptr); ret < 0) return fail(media::ffError(ret, "open decoder"));
    return d;
}

// onFrame returns false to stop decoding early.
template <class OnFrame>
Status decodeAll(Decoder& d, OnFrame&& onFrame) {
    bool keepGoing = true;
    media::Packet pkt = media::Packet::alloc();
    media::Frame frame = media::Frame::alloc();
    auto drain = [&]() -> Status {
        for (;;) {
            const int ret = avcodec_receive_frame(d.ctx.get(), frame.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return ok();
            if (ret < 0) return fail(media::ffError(ret, "decode"));
            if (keepGoing) keepGoing = onFrame(frame.get());
            av_frame_unref(frame.get());
        }
    };
    while (keepGoing && av_read_frame(d.fmt.get(), pkt.get()) >= 0) {
        if (pkt->stream_index == d.stream) {
            avcodec_send_packet(d.ctx.get(), pkt.get());
            LEC_TRY(drain());
        }
        av_packet_unref(pkt.get());
    }
    avcodec_send_packet(d.ctx.get(), nullptr);
    return drain();
}

}  // namespace

namespace {
void seekTo(Decoder& d, double startSeconds) {
    if (startSeconds <= 0) return;
    const AVRational tb = d.fmt->streams[d.stream]->time_base;
    av_seek_frame(d.fmt.get(), d.stream, static_cast<std::int64_t>(startSeconds / av_q2d(tb)), AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(d.ctx.get());
}
}  // namespace

Result<DecodedVideo> decodeVideo(const std::filesystem::path& path, double startSeconds, double durationSeconds) {
    auto d = openDecoder(path, AVMEDIA_TYPE_VIDEO);
    if (!d) return fail(std::move(d).error());
    DecodedVideo out;
    const AVRational tb = d->fmt->streams[d->stream]->time_base;
    seekTo(*d, startSeconds);
    const double end = startSeconds >= 0 && durationSeconds > 0 ? startSeconds + durationSeconds : 1e300;
    LEC_TRY(decodeAll(*d, [&](AVFrame* f) {
        out.width = f->width;
        out.height = f->height;
        const std::int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
        const double t = static_cast<double>(pts) * av_q2d(tb);
        if (t < startSeconds - 1e-6) return true;
        if (t >= end) return false;
        out.ptsSeconds.push_back(t);
        double sum = 0;
        int count = 0;
        for (int y = 0; y < f->height; y += 4) {
            for (int x = 0; x < f->width; x += 4) {
                sum += f->data[0][y * f->linesize[0] + x];
                ++count;
            }
        }
        out.meanLuma.push_back(count ? sum / count : 0);
        out.keyframes.push_back((f->flags & AV_FRAME_FLAG_KEY) != 0);
        return true;
    }));
    return out;
}

Result<DecodedAudio> decodeAudio(const std::filesystem::path& path, double startSeconds, double durationSeconds) {
    auto d = openDecoder(path, AVMEDIA_TYPE_AUDIO);
    if (!d) return fail(std::move(d).error());
    DecodedAudio out;
    out.sampleRate = d->ctx->sample_rate;
    out.channels = d->ctx->ch_layout.nb_channels;
    const AVRational tb = d->fmt->streams[d->stream]->time_base;
    seekTo(*d, startSeconds);
    const double end = startSeconds >= 0 && durationSeconds > 0 ? startSeconds + durationSeconds : 1e300;
    bool first = true;
    LEC_TRY(decodeAll(*d, [&](AVFrame* f) {
        const std::int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
        const double t = static_cast<double>(pts) * av_q2d(tb);
        if (t + static_cast<double>(f->nb_samples) / f->sample_rate < startSeconds) return true;
        if (t >= end) return false;
        if (first) {
            out.startSeconds = t;
            first = false;
        }
        const auto fmt = static_cast<AVSampleFormat>(f->format);
        const bool planar = av_sample_fmt_is_planar(fmt);
        const int stride = planar ? 1 : f->ch_layout.nb_channels;
        for (int i = 0; i < f->nb_samples; ++i) {
            float v = 0;
            const int idx = i * stride;
            switch (av_get_packed_sample_fmt(fmt)) {
                case AV_SAMPLE_FMT_S16: v = reinterpret_cast<const std::int16_t*>(f->data[0])[idx] / 32768.0f; break;
                case AV_SAMPLE_FMT_S32: v = static_cast<float>(reinterpret_cast<const std::int32_t*>(f->data[0])[idx] / 2147483648.0); break;
                case AV_SAMPLE_FMT_FLT: v = reinterpret_cast<const float*>(f->data[0])[idx]; break;
                default: break;
            }
            out.channel0.push_back(v);
        }
        return true;
    }));
    return out;
}

std::uint64_t currentRssBytes() {
#if defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return info.resident_size;
    }
    return 0;
#elif defined(__linux__)
    long pages = 0;
    if (FILE* f = std::fopen("/proc/self/statm", "r")) {
        long size = 0;
        if (std::fscanf(f, "%ld %ld", &size, &pages) != 2) pages = 0;
        std::fclose(f);
    }
    return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
#else
    return 0;
#endif
}

std::vector<double> risingEdges(const std::vector<double>& times, const std::vector<bool>& flags) {
    std::vector<double> out;
    bool prev = false;
    for (std::size_t i = 0; i < times.size() && i < flags.size(); ++i) {
        if (flags[i] && !prev) out.push_back(times[i]);
        prev = flags[i];
    }
    return out;
}

std::vector<double> burstStarts(const DecodedAudio& audio, float threshold, double minGapSeconds) {
    std::vector<double> out;
    double last = -1e9;
    for (std::size_t i = 0; i < audio.channel0.size(); ++i) {
        if (std::fabs(audio.channel0[i]) > threshold) {
            const double t = audio.startSeconds + static_cast<double>(i) / audio.sampleRate;
            if (t - last >= minGapSeconds) out.push_back(t);
            last = t;
        }
    }
    return out;
}

}  // namespace lectern::test
