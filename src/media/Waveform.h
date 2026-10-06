#pragma once

#include "core/Error.h"
#include "core/Time.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace lectern::media {

/// Loudness overview of an audio file for drawing waveforms: one byte per
/// bin, the bin's peak mapped from −60…0 dBFS to 0…255 (speech stays visible
/// next to loud music).
struct WaveformPeaks {
    Time start;             ///< media time of the first bin
    int binsPerSecond = 100;
    std::vector<std::uint8_t> peaks;

    [[nodiscard]] Time duration() const { return Time::fromSamples(static_cast<std::int64_t>(peaks.size()), binsPerSecond); }
    /// Highest bin value over media time [from, to); 0 outside the file.
    [[nodiscard]] std::uint8_t maxIn(Time from, Time to) const;
};

[[nodiscard]] std::uint8_t peakToByte(float peak) noexcept;

/// Decodes the file's audio once (both channels) and returns its peaks.
[[nodiscard]] Result<WaveformPeaks> computeWaveform(const std::filesystem::path& path, int binsPerSecond = 100,
                                                    const std::atomic<bool>* cancel = nullptr);

}  // namespace lectern::media
