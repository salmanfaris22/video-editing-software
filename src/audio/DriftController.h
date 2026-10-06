#pragma once

#include <cstdint>

namespace lectern::audio {

/// Keeps a sample-counted audio file aligned with host time
/// (docs/RECORDING_ENGINE.md §6.2).
///
/// Inputs per chunk, in output samples:
///   target = where the chunk's first sample *should* land (session time × rate)
///   actual = where it *will* land (samples produced + resampler backlog)
/// Small errors are corrected softly through the resampling ratio over a one
/// second horizon (proportional control). Large errors (lost buffers, device
/// stalls) are corrected immediately by inserting silence or dropping input.
class DriftController {
public:
    struct Config {
        int outputRate = 48'000;
        double hardThresholdSamples = 960;  ///< 20 ms @ 48 kHz
        int compensationDistance = 48'000;  ///< correct over ~1 s
        double maxRatio = 0.005;            ///< ±0.5 % resampling ratio change
    };

    enum class Action { None, Soft, InsertSilence, DropInput };

    struct Decision {
        Action action = Action::None;
        /// Soft: sample delta for swr_set_compensation (positive = stretch).
        /// InsertSilence / DropInput: number of *output* samples.
        std::int64_t samples = 0;
        int compensationDistance = 0;
        double errorSamples = 0;
    };

    explicit DriftController(const Config& config) noexcept : config_(config) {}

    [[nodiscard]] Decision evaluate(double targetPosition, double actualPosition) noexcept;

    [[nodiscard]] std::uint64_t hardCorrections() const noexcept { return hardCorrections_; }
    [[nodiscard]] double lastErrorSamples() const noexcept { return lastError_; }
    [[nodiscard]] double maxAbsSoftErrorSamples() const noexcept { return maxAbsSoftError_; }
    [[nodiscard]] const Config& config() const noexcept { return config_; }

private:
    Config config_;
    std::uint64_t hardCorrections_ = 0;
    double lastError_ = 0;
    double maxAbsSoftError_ = 0;
};

}  // namespace lectern::audio
