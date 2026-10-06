#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace lectern::audio {

inline constexpr int kMeterChannels = 2;
inline constexpr float kSilenceDb = -120.0f;

struct ChannelLevel {
    float peakDb = kSilenceDb;  ///< peak with 20 dB/s fall-back
    float rmsDb = kSilenceDb;   ///< 300 ms exponential RMS
    bool clipped = false;       ///< any sample at/above full scale since last reset
};

struct LevelSnapshot {
    int channels = 0;
    std::array<ChannelLevel, kMeterChannels> channel{};
    std::int64_t updatedAtNs = 0;

    /// Loudest channel peak, convenient for a single mono meter.
    [[nodiscard]] float maxPeakDb() const noexcept;
};

/// Peak/RMS meter. `process` runs on one consumer thread (the audio pump);
/// `snapshot` may be called from any thread (values are published through
/// relaxed atomics; a snapshot can mix two consecutive updates, which is
/// harmless for a display).
class LevelMeter {
public:
    explicit LevelMeter(int sampleRate = 48'000) noexcept : sampleRate_(sampleRate) {}

    void process(const float* interleaved, int frames, int channels, std::int64_t nowNs) noexcept;
    [[nodiscard]] LevelSnapshot snapshot() const noexcept;
    void clearClip() noexcept;
    void setSampleRate(int rate) noexcept { sampleRate_ = rate > 0 ? rate : 48'000; }

private:
    int sampleRate_;
    // Consumer-thread state.
    std::array<float, kMeterChannels> peak_{};
    std::array<double, kMeterChannels> meanSquare_{};
    // Published values.
    std::array<std::atomic<float>, kMeterChannels> peakDb_{};
    std::array<std::atomic<float>, kMeterChannels> rmsDb_{};
    std::array<std::atomic<bool>, kMeterChannels> clipped_{};
    std::atomic<int> channels_{0};
    std::atomic<std::int64_t> updatedAt_{0};
};

[[nodiscard]] float linearToDb(float linear) noexcept;

}  // namespace lectern::audio
