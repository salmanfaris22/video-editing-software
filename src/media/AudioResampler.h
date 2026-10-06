#pragma once

#include "media/Frame.h"

#include <memory>

namespace lectern::media {

/// Interleaved float32 → interleaved float32 resampler with drift
/// compensation (docs/RECORDING_ENGINE.md §6.2).
///
/// Compensation stretches or squeezes the output by `sampleDelta` samples over
/// the next `distance` output samples (swr_set_compensation). Resampling is
/// forced on even at equal rates so compensation is always possible.
class AudioResampler {
public:
    struct Config {
        int inRate = 48'000;
        int inChannels = 2;
        int outRate = 48'000;
        int outChannels = 2;
        bool forceResampling = true;
    };

    static Result<std::unique_ptr<AudioResampler>> create(const Config& config);

    /// Converts `inFrames`; writes at most `outCapacity` frames. Returns frames written.
    Result<int> process(const float* in, int inFrames, float* out, int outCapacity);
    /// Returns buffered output at end of stream.
    Result<int> drain(float* out, int outCapacity);
    /// See class comment. Clamped by the caller (DriftController).
    Status setCompensation(int sampleDelta, int distance);
    /// Output samples still buffered inside the resampler (rounded up).
    [[nodiscard]] std::int64_t bufferedOutputSamples() const;
    /// Exact (fractional) delay of the next input sample, in output samples.
    [[nodiscard]] double delayOutputSamples() const;
    /// Upper bound on output frames for `inFrames` input frames.
    [[nodiscard]] int maxOutputFrames(int inFrames) const;

    [[nodiscard]] const Config& config() const noexcept { return config_; }

private:
    AudioResampler() = default;
    Config config_;
    SwrContextPtr swr_;
};

}  // namespace lectern::media
