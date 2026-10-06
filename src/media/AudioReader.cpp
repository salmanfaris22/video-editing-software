#include "media/AudioReader.h"

#include <algorithm>
#include <cstring>

namespace lectern::media {

namespace {
constexpr std::int64_t kSeekAheadSeconds = 2;  // forward jumps beyond this seek
}  // namespace

Result<std::unique_ptr<AudioReader>> AudioReader::open(const std::filesystem::path& path, int sampleRate) {
    std::unique_ptr<AudioReader> r(new AudioReader());
    r->rate_ = sampleRate;
    AVFormatContext* raw = nullptr;
    if (const int ret = avformat_open_input(&raw, path.string().c_str(), nullptr, nullptr); ret < 0) {
        return fail(ffError(ret, "open " + path.filename().string(), ret == AVERROR(ENOENT) ? ErrorCode::NotFound
                                                                                          : ErrorCode::IoError));
    }
    r->format_.reset(raw);
    if (const int ret = avformat_find_stream_info(raw, nullptr); ret < 0) return fail(ffError(ret, "read stream info"));
    const AVCodec* codec = nullptr;
    r->stream_ = av_find_best_stream(raw, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (r->stream_ < 0 || !codec) return fail(ErrorCode::NotFound, path.filename().string() + " has no audio");
    const AVStream* st = raw->streams[r->stream_];
    r->timeBase_ = st->time_base;

    r->codec_.reset(avcodec_alloc_context3(codec));
    if (!r->codec_) return fail(ErrorCode::OutOfMemory, "avcodec_alloc_context3");
    if (const int ret = avcodec_parameters_to_context(r->codec_.get(), st->codecpar); ret < 0) {
        return fail(ffError(ret, "decoder parameters"));
    }
    r->codec_->pkt_timebase = st->time_base;
    if (const int ret = avcodec_open2(r->codec_.get(), codec, nullptr); ret < 0) return fail(ffError(ret, "open decoder"));
    r->sourceChannels_ = r->codec_->ch_layout.nb_channels;
    if (r->codec_->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
        av_channel_layout_default(&r->codec_->ch_layout, std::max(1, r->sourceChannels_));
    }

    // Converter: anything → stereo float at the output rate.
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    SwrContext* swr = nullptr;
    if (const int ret = swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, sampleRate, &r->codec_->ch_layout,
                                            r->codec_->sample_fmt, r->codec_->sample_rate, 0, nullptr);
        ret < 0) {
        return fail(ffError(ret, "audio converter"));
    }
    r->swr_.reset(swr);
    LEC_TRY(r->initConverter());

    r->frame_ = Frame::alloc();
    if (!r->frame_) return fail(ErrorCode::OutOfMemory, "av_frame_alloc");
    r->start_ = st->start_time != AV_NOPTS_VALUE ? Time::fromRational(st->start_time, Rational(st->time_base.num, st->time_base.den))
                                                 : Time::zero();
    if (st->duration > 0) {
        r->duration_ = Time::fromRational(st->duration, Rational(st->time_base.num, st->time_base.den));
    } else if (raw->duration > 0) {
        r->duration_ = Time::fromMicroseconds(raw->duration) - r->start_;
    }
    return r;
}

AudioReader::~AudioReader() = default;

Status AudioReader::initConverter() {
    // FFmpeg spreads pure mono at -3 dB per side (power-preserving); a voice
    // recording should play at full level on both speakers instead.
    if (sourceChannels_ == 1) {
        static constexpr double kMonoToStereo[2] = {1.0, 1.0};
        if (const int ret = swr_set_matrix(swr_.get(), kMonoToStereo, 1); ret < 0) return fail(ffError(ret, "audio matrix"));
    }
    if (const int ret = swr_init(swr_.get()); ret < 0) return fail(ffError(ret, "init audio converter"));
    return ok();
}

Status AudioReader::seek(std::int64_t sample) {
    ++seeks_;
    const Time t = std::max(Time::fromSamples(sample, rate_), start_);
    const std::int64_t ts = t.toRational(Rational(timeBase_.num, timeBase_.den), Rounding::Floor);
    if (const int ret = av_seek_frame(format_.get(), stream_, ts, AVSEEK_FLAG_BACKWARD); ret < 0) {
        if (const int r2 = av_seek_frame(format_.get(), stream_, 0, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY); r2 < 0) {
            return fail(ffError(ret, "seek audio"));
        }
    }
    avcodec_flush_buffers(codec_.get());
    swr_close(swr_.get());
    LEC_TRY(initConverter());
    buffer_.clear();
    positioned_ = false;
    eof_ = false;
    drained_ = false;
    return ok();
}

void AudioReader::dropBefore(std::int64_t sample) {
    if (!positioned_ || sample <= bufferStart_) return;
    const auto frames = static_cast<std::size_t>(std::min<std::int64_t>(sample - bufferStart_,
                                                                         static_cast<std::int64_t>(buffer_.size() / kChannels)));
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(frames * kChannels));
    bufferStart_ += static_cast<std::int64_t>(frames);
}

Result<bool> AudioReader::decodeMore() {
    for (;;) {
        const int ret = avcodec_receive_frame(codec_.get(), frame_.get());
        if (ret == 0) break;
        if (ret == AVERROR_EOF) {
            drained_ = true;
            return false;
        }
        if (ret != AVERROR(EAGAIN)) return fail(ffError(ret, "decode audio"));
        if (eof_) {
            if (const int s = avcodec_send_packet(codec_.get(), nullptr); s < 0 && s != AVERROR_EOF) {
                return fail(ffError(s, "drain audio decoder"));
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
        if (r < 0) return fail(ffError(r, "read audio packet"));
        if (pkt->stream_index != stream_) continue;
        if (const int s = avcodec_send_packet(codec_.get(), pkt.get()); s < 0 && s != AVERROR(EAGAIN)) {
            if (s == AVERROR_INVALIDDATA) continue;
            return fail(ffError(s, "send audio packet"));
        }
    }

    // Where this frame belongs on the media timeline, at the output rate.
    const std::int64_t pts = frame_->best_effort_timestamp != AV_NOPTS_VALUE ? frame_->best_effort_timestamp : frame_->pts;
    const int outCapacity = swr_get_out_samples(swr_.get(), frame_->nb_samples);
    std::vector<float> converted(static_cast<std::size_t>(std::max(0, outCapacity)) * kChannels);
    std::uint8_t* outPlanes[1] = {reinterpret_cast<std::uint8_t*>(converted.data())};
    const int produced = swr_convert(swr_.get(), outPlanes, outCapacity, frame_->extended_data, frame_->nb_samples);
    av_frame_unref(frame_.get());
    if (produced < 0) return fail(ffError(produced, "convert audio"));
    if (produced == 0) return true;

    if (pts != AV_NOPTS_VALUE) {
        const std::int64_t at =
            Time::fromRational(pts, Rational(timeBase_.num, timeBase_.den)).toSamples(rate_, Rounding::Nearest);
        const std::int64_t end = bufferStart_ + static_cast<std::int64_t>(buffer_.size() / kChannels);
        if (!positioned_) {
            bufferStart_ = at;
            positioned_ = true;
        } else if (at > end + rate_ / 100) {
            buffer_.resize(buffer_.size() + static_cast<std::size_t>(at - end) * kChannels, 0.0f);  // gap: silence
        }
    } else if (!positioned_) {
        bufferStart_ = start_.toSamples(rate_);
        positioned_ = true;
    }
    buffer_.insert(buffer_.end(), converted.begin(), converted.begin() + static_cast<std::ptrdiff_t>(produced) * kChannels);
    return true;
}

Status AudioReader::read(Time t, int frames, float* out) {
    if (frames <= 0) return ok();
    const std::int64_t want = t.toSamples(rate_, Rounding::Nearest);
    const std::int64_t wantEnd = want + frames;
    const std::int64_t mediaStart = start_.toSamples(rate_);
    const std::int64_t mediaEnd = duration_ > Time::zero() ? (start_ + duration_).toSamples(rate_) + rate_ : INT64_MAX;
    std::memset(out, 0, static_cast<std::size_t>(frames) * kChannels * sizeof(float));
    if (wantEnd <= mediaStart || want >= mediaEnd) return ok();  // no audio there

    const std::int64_t bufferEnd = bufferStart_ + static_cast<std::int64_t>(buffer_.size() / kChannels);
    const bool contiguous = positioned_ && want >= bufferStart_ && want <= bufferEnd + rate_ * kSeekAheadSeconds;
    if (!contiguous) LEC_TRY(seek(std::max(want, mediaStart)));

    while (!drained_ && (!positioned_ || bufferStart_ + static_cast<std::int64_t>(buffer_.size() / kChannels) < wantEnd)) {
        auto more = decodeMore();
        if (!more) return fail(std::move(more).error());
        if (!*more) break;
        // Keep memory bounded while skipping forward to the wanted position.
        if (positioned_ && want - bufferStart_ > rate_) dropBefore(want - rate_ / 10);
    }
    if (!positioned_) return ok();
    const std::int64_t have = static_cast<std::int64_t>(buffer_.size() / kChannels);
    const std::int64_t from = std::max(want, bufferStart_);
    const std::int64_t to = std::min(wantEnd, bufferStart_ + have);
    if (to > from) {
        std::memcpy(out + (from - want) * kChannels, buffer_.data() + (from - bufferStart_) * kChannels,
                    static_cast<std::size_t>(to - from) * kChannels * sizeof(float));
    }
    // Keep a little history for overlapping reads (e.g. crossfades), drop the rest.
    dropBefore(wantEnd - rate_ / 10);
    return ok();
}

}  // namespace lectern::media
