#include "media/AudioResampler.h"

namespace lectern::media {

Result<std::unique_ptr<AudioResampler>> AudioResampler::create(const Config& config) {
    if (config.inRate <= 0 || config.outRate <= 0 || config.inChannels <= 0 || config.outChannels <= 0) {
        return fail(ErrorCode::InvalidArgument, "bad resampler configuration");
    }
    AVChannelLayout inLayout{};
    AVChannelLayout outLayout{};
    av_channel_layout_default(&inLayout, config.inChannels);
    av_channel_layout_default(&outLayout, config.outChannels);
    SwrContext* swr = nullptr;
    int ret = swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, config.outRate, &inLayout, AV_SAMPLE_FMT_FLT,
                                  config.inRate, 0, nullptr);
    av_channel_layout_uninit(&inLayout);
    av_channel_layout_uninit(&outLayout);
    if (ret < 0) return fail(ffError(ret, "swr_alloc_set_opts2"));
    if (config.forceResampling) av_opt_set_int(swr, "flags", SWR_FLAG_RESAMPLE, 0);
    if ((ret = swr_init(swr)) < 0) {
        swr_free(&swr);
        return fail(ffError(ret, "swr_init"));
    }
    std::unique_ptr<AudioResampler> r(new AudioResampler());
    r->config_ = config;
    r->swr_.reset(swr);
    return r;
}

Result<int> AudioResampler::process(const float* in, int inFrames, float* out, int outCapacity) {
    const auto* inPtr = reinterpret_cast<const std::uint8_t*>(in);
    auto* outPtr = reinterpret_cast<std::uint8_t*>(out);
    const int n = swr_convert(swr_.get(), &outPtr, outCapacity, &inPtr, inFrames);
    if (n < 0) return fail(ffError(n, "resample"));
    return n;
}

Result<int> AudioResampler::drain(float* out, int outCapacity) {
    auto* outPtr = reinterpret_cast<std::uint8_t*>(out);
    const int n = swr_convert(swr_.get(), &outPtr, outCapacity, nullptr, 0);
    if (n < 0) return fail(ffError(n, "drain resampler"));
    return n;
}

Status AudioResampler::setCompensation(int sampleDelta, int distance) {
    if (const int ret = swr_set_compensation(swr_.get(), sampleDelta, distance); ret < 0) {
        return fail(ffError(ret, "swr_set_compensation"));
    }
    return ok();
}

std::int64_t AudioResampler::bufferedOutputSamples() const { return swr_get_delay(swr_.get(), config_.outRate); }

double AudioResampler::delayOutputSamples() const {
    // base = in*out gives a rounding-free delay (see swr_get_delay docs).
    const std::int64_t base = static_cast<std::int64_t>(config_.inRate) * config_.outRate;
    return static_cast<double>(swr_get_delay(swr_.get(), base)) / config_.inRate;
}

int AudioResampler::maxOutputFrames(int inFrames) const {
    const int n = swr_get_out_samples(swr_.get(), inFrames);
    return n > 0 ? n + 32 : inFrames * (config_.outRate / config_.inRate + 1) + 256;
}

}  // namespace lectern::media
