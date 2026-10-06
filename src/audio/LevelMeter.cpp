#include "audio/LevelMeter.h"

#include <algorithm>
#include <cmath>

namespace lectern::audio {

namespace {
constexpr float kFallDbPerSecond = 20.0f;
constexpr double kRmsTimeConstantSeconds = 0.3;
constexpr float kClipLevel = 0.999f;
}  // namespace

float linearToDb(float linear) noexcept {
    if (linear <= 1e-6f) return kSilenceDb;
    return std::max(kSilenceDb, 20.0f * std::log10(linear));
}

float LevelSnapshot::maxPeakDb() const noexcept {
    float best = kSilenceDb;
    for (int c = 0; c < std::min(channels, kMeterChannels); ++c) best = std::max(best, channel[static_cast<std::size_t>(c)].peakDb);
    return best;
}

void LevelMeter::process(const float* interleaved, int frames, int channels, std::int64_t nowNs) noexcept {
    if (frames <= 0 || channels <= 0 || !interleaved) return;
    const int metered = std::min(channels, kMeterChannels);
    const double blockSeconds = static_cast<double>(frames) / sampleRate_;
    const float fall = std::pow(10.0f, -kFallDbPerSecond * static_cast<float>(blockSeconds) / 20.0f);
    const double alpha = 1.0 - std::exp(-blockSeconds / kRmsTimeConstantSeconds);

    for (int c = 0; c < metered; ++c) {
        float blockPeak = 0.0f;
        double sumSquares = 0.0;
        for (int i = 0; i < frames; ++i) {
            const float s = interleaved[static_cast<std::size_t>(i) * static_cast<std::size_t>(channels) +
                                        static_cast<std::size_t>(c)];
            const float a = std::fabs(s);
            blockPeak = std::max(blockPeak, a);
            sumSquares += static_cast<double>(s) * s;
        }
        const auto ci = static_cast<std::size_t>(c);
        peak_[ci] = std::max(blockPeak, peak_[ci] * fall);
        meanSquare_[ci] += alpha * (sumSquares / frames - meanSquare_[ci]);
        peakDb_[ci].store(linearToDb(peak_[ci]), std::memory_order_relaxed);
        rmsDb_[ci].store(linearToDb(static_cast<float>(std::sqrt(meanSquare_[ci]))), std::memory_order_relaxed);
        if (blockPeak >= kClipLevel) clipped_[ci].store(true, std::memory_order_relaxed);
    }
    channels_.store(metered, std::memory_order_relaxed);
    updatedAt_.store(nowNs, std::memory_order_relaxed);
}

LevelSnapshot LevelMeter::snapshot() const noexcept {
    LevelSnapshot s;
    s.channels = channels_.load(std::memory_order_relaxed);
    for (std::size_t c = 0; c < static_cast<std::size_t>(kMeterChannels); ++c) {
        s.channel[c].peakDb = peakDb_[c].load(std::memory_order_relaxed);
        s.channel[c].rmsDb = rmsDb_[c].load(std::memory_order_relaxed);
        s.channel[c].clipped = clipped_[c].load(std::memory_order_relaxed);
    }
    s.updatedAtNs = updatedAt_.load(std::memory_order_relaxed);
    return s;
}

void LevelMeter::clearClip() noexcept {
    for (auto& c : clipped_) c.store(false, std::memory_order_relaxed);
}

}  // namespace lectern::audio
