#include "media/AudioEncoder.h"

#include <algorithm>

namespace lectern::media {

std::string_view toString(AudioCodec codec) noexcept {
    switch (codec) {
        case AudioCodec::Flac: return "flac";
        case AudioCodec::Aac: return "aac";
        case AudioCodec::Pcm: return "pcm";
    }
    return "flac";
}

namespace {

std::vector<std::string> candidatesFor(const AudioEncoderConfig& cfg) {
    if (!cfg.encoderNames.empty()) return cfg.encoderNames;
    switch (cfg.codec) {
        case AudioCodec::Flac: return {"flac"};
        case AudioCodec::Aac:
#if defined(__APPLE__)
            return {"aac_at", "aac"};
#else
            return {"aac"};
#endif
        case AudioCodec::Pcm: return {cfg.bitsPerSample > 16 ? "pcm_s24le" : "pcm_s16le"};
    }
    return {"flac"};
}

AVSampleFormat chooseSampleFormat(const AVCodecContext* ctx, const AVCodec* codec, const AudioEncoderConfig& cfg) {
    const void* configs = nullptr;
    int count = 0;
    std::vector<AVSampleFormat> supported;
    if (avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &configs, &count) >= 0 && configs) {
        const auto* fmts = static_cast<const AVSampleFormat*>(configs);
        supported.assign(fmts, fmts + count);
    }
    const bool deep = cfg.bitsPerSample > 16;
    const AVSampleFormat prefs[] = {
        deep ? AV_SAMPLE_FMT_S32 : AV_SAMPLE_FMT_S16, deep ? AV_SAMPLE_FMT_S32P : AV_SAMPLE_FMT_S16P,
        AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_S16,
    };
    if (supported.empty()) return prefs[0];
    for (AVSampleFormat p : prefs) {
        if (std::find(supported.begin(), supported.end(), p) != supported.end()) return p;
    }
    return supported.front();
}

}  // namespace

Result<std::unique_ptr<AudioEncoder>> AudioEncoder::create(const AudioEncoderConfig& config) {
    if (config.sampleRate <= 0 || config.channels <= 0 || config.channels > 8) {
        return fail(ErrorCode::InvalidArgument, "bad audio encoder format");
    }
    std::string attempts;
    for (const std::string& name : candidatesFor(config)) {
        const AVCodec* codec = avcodec_find_encoder_by_name(name.c_str());
        if (!codec) {
            attempts += name + ": not built; ";
            continue;
        }
        AVCodecContextPtr ctx(avcodec_alloc_context3(codec));
        if (!ctx) return fail(ErrorCode::OutOfMemory, "avcodec_alloc_context3");
        ctx->sample_rate = config.sampleRate;
        av_channel_layout_default(&ctx->ch_layout, config.channels);
        ctx->time_base = AVRational{1, config.sampleRate};
        ctx->sample_fmt = chooseSampleFormat(ctx.get(), codec, config);
        ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (config.codec == AudioCodec::Aac) ctx->bit_rate = config.bitRate;
        if (ctx->sample_fmt == AV_SAMPLE_FMT_S32 || ctx->sample_fmt == AV_SAMPLE_FMT_S32P) {
            ctx->bits_per_raw_sample = std::clamp(config.bitsPerSample, 16, 32);
        }

        Dictionary opts;
        if (name == "flac") opts.set("compression_level", "5");
        if (const int ret = avcodec_open2(ctx.get(), codec, opts.address()); ret < 0) {
            attempts += name + ": " + ffErrorString(ret) + "; ";
            continue;
        }

        std::unique_ptr<AudioEncoder> enc(new AudioEncoder());
        enc->name_ = name;
        enc->frameSize_ = ctx->frame_size > 0 ? ctx->frame_size : 1024;

        AVChannelLayout layout{};
        av_channel_layout_default(&layout, config.channels);
        SwrContext* swr = nullptr;
        int ret = swr_alloc_set_opts2(&swr, &layout, ctx->sample_fmt, config.sampleRate, &layout, AV_SAMPLE_FMT_FLT,
                                      config.sampleRate, 0, nullptr);
        av_channel_layout_uninit(&layout);
        if (ret < 0 || (ret = swr_init(swr)) < 0) {
            swr_free(&swr);
            return fail(ffError(ret, "audio format converter"));
        }
        enc->convert_.reset(swr);
        enc->fifo_.reset(av_audio_fifo_alloc(ctx->sample_fmt, config.channels, enc->frameSize_ * 4));
        if (!enc->fifo_) return fail(ErrorCode::OutOfMemory, "av_audio_fifo_alloc");

        auto frame = Frame::allocAudio(enc->frameSize_, ctx->sample_fmt, ctx->ch_layout, config.sampleRate);
        if (!frame) return fail(std::move(frame).error());
        enc->frame_ = std::move(*frame);
        enc->planes_.resize(static_cast<std::size_t>(config.channels));
        enc->ctx_ = std::move(ctx);
        LEC_DEBUG("media", "audio encoder {} {} Hz x{} {} frame {}", name, config.sampleRate, config.channels,
                  sampleFormatName(enc->ctx_->sample_fmt), enc->frameSize_);
        return enc;
    }
    return fail(ErrorCode::EncoderError, "no usable audio encoder: " + attempts);
}

AudioEncoder::~AudioEncoder() = default;

Status AudioEncoder::pushFifo(const float* interleaved, int frames) {
    if (frames <= 0) return ok();
    const int channels = ctx_->ch_layout.nb_channels;
    const bool planar = av_sample_fmt_is_planar(ctx_->sample_fmt) != 0;
    const int bytesPerSample = av_get_bytes_per_sample(ctx_->sample_fmt);

    // Convert into a reusable scratch buffer, then append to the FIFO
    // (allocation-free once the scratch buffer has grown to the chunk size).
    const std::size_t needed = static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels) *
                               static_cast<std::size_t>(bytesPerSample);
    if (scratch_.size() < needed) scratch_.resize(needed);
    for (int c = 0; c < channels; ++c) {
        planes_[static_cast<std::size_t>(c)] =
            planar ? scratch_.data() + static_cast<std::size_t>(c) * static_cast<std::size_t>(frames) *
                                           static_cast<std::size_t>(bytesPerSample)
                   : scratch_.data();
    }
    const auto* in = reinterpret_cast<const std::uint8_t*>(interleaved);
    const int converted = swr_convert(convert_.get(), planes_.data(), frames, &in, frames);
    if (converted < 0) return fail(ffError(converted, "convert audio samples"));
    if (av_audio_fifo_write(fifo_.get(), reinterpret_cast<void* const*>(planes_.data()), converted) < converted) {
        return fail(ErrorCode::OutOfMemory, "audio fifo write");
    }
    return ok();
}

Status AudioEncoder::sendFrame(AVFrame* frame, const PacketSink& sink) {
    int ret = avcodec_send_frame(ctx_.get(), frame);
    if (ret < 0 && ret != AVERROR_EOF) return fail(ffError(ret, "send audio frame", ErrorCode::EncoderError));
    for (;;) {
        Packet pkt = Packet::alloc();
        if (!pkt) return fail(ErrorCode::OutOfMemory, "av_packet_alloc");
        ret = avcodec_receive_packet(ctx_.get(), pkt.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return ok();
        if (ret < 0) return fail(ffError(ret, "receive audio packet", ErrorCode::EncoderError));
        LEC_TRY(sink(std::move(pkt)));
    }
}

Status AudioEncoder::encodeAvailable(bool final, const PacketSink& sink) {
    const bool smallLast = (ctx_->codec->capabilities &
                            (AV_CODEC_CAP_SMALL_LAST_FRAME | AV_CODEC_CAP_VARIABLE_FRAME_SIZE)) != 0;
    for (;;) {
        const int available = av_audio_fifo_size(fifo_.get());
        if (available <= 0) return ok();
        if (available < frameSize_ && !final) return ok();
        int take = std::min(available, frameSize_);
        if (const int ret = av_frame_make_writable(frame_.get()); ret < 0) {
            return fail(ffError(ret, "make audio frame writable"));
        }
        frame_->nb_samples = frameSize_;
        const int read = av_audio_fifo_read(fifo_.get(), reinterpret_cast<void* const*>(frame_->data), take);
        if (read < take) return fail(ErrorCode::Internal, "audio fifo read");
        if (take < frameSize_) {
            if (smallLast) {
                frame_->nb_samples = take;
            } else {
                // Codec needs full frames: pad the tail with silence.
                av_samples_set_silence(frame_->data, take, frameSize_ - take, ctx_->ch_layout.nb_channels,
                                       ctx_->sample_fmt);
                take = frameSize_;
            }
        }
        frame_->pts = nextPts_;
        nextPts_ += frame_->nb_samples;
        LEC_TRY(sendFrame(frame_.get(), sink));
    }
}

Status AudioEncoder::encode(const float* interleaved, int frames, const PacketSink& sink) {
    if (flushed_) return fail(ErrorCode::InvalidState, "encode after flush");
    LEC_TRY(pushFifo(interleaved, frames));
    submitted_ += frames;
    return encodeAvailable(false, sink);
}

Status AudioEncoder::encodeSilence(int frames, const PacketSink& sink) {
    const int channels = ctx_->ch_layout.nb_channels;
    constexpr int kChunk = 4800;
    if (silence_.size() < static_cast<std::size_t>(kChunk * channels)) {
        silence_.assign(static_cast<std::size_t>(kChunk * channels), 0.0f);
    }
    while (frames > 0) {
        const int n = std::min(frames, kChunk);
        LEC_TRY(encode(silence_.data(), n, sink));
        frames -= n;
    }
    return ok();
}

Status AudioEncoder::flush(const PacketSink& sink) {
    if (flushed_) return ok();
    LEC_TRY(encodeAvailable(true, sink));
    flushed_ = true;
    return sendFrame(nullptr, sink);
}

}  // namespace lectern::media
