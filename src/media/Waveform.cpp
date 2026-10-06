#include "media/Waveform.h"

#include "media/AudioReader.h"

#include <algorithm>
#include <cmath>

namespace lectern::media {

std::uint8_t peakToByte(float peak) noexcept {
    if (!(peak > 0.0f)) return 0;
    const double db = 20.0 * std::log10(static_cast<double>(std::min(peak, 1.0f)));
    return static_cast<std::uint8_t>(std::clamp(std::lround((db + 60.0) / 60.0 * 255.0), 0L, 255L));
}

std::uint8_t WaveformPeaks::maxIn(Time from, Time to) const {
    if (peaks.empty() || to <= from) return 0;
    auto bin = [this](Time t) { return (t - start).toSamples(binsPerSecond, Rounding::Floor); };
    const std::int64_t a = std::max<std::int64_t>(0, bin(from));
    const std::int64_t b = std::min<std::int64_t>(static_cast<std::int64_t>(peaks.size()), std::max(a + 1, bin(to)));
    std::uint8_t best = 0;
    for (std::int64_t i = a; i < b; ++i) best = std::max(best, peaks[static_cast<std::size_t>(i)]);
    return best;
}

Result<WaveformPeaks> computeWaveform(const std::filesystem::path& path, int binsPerSecond, const std::atomic<bool>* cancel) {
    binsPerSecond = std::clamp(binsPerSecond, 1, 1000);
    auto reader = AudioReader::open(path);
    if (!reader) return fail(std::move(reader).error());
    WaveformPeaks out;
    out.start = (*reader)->start();
    out.binsPerSecond = binsPerSecond;
    const int rate = (*reader)->sampleRate();
    const int binFrames = std::max(1, rate / binsPerSecond);
    const std::int64_t firstSample = out.start.toSamples(rate);
    const std::int64_t total = (*reader)->duration().toSamples(rate);
    constexpr int kBinsPerRead = 50;
    std::vector<float> buffer(static_cast<std::size_t>(binFrames) * kBinsPerRead * AudioReader::kChannels);
    out.peaks.reserve(static_cast<std::size_t>(total / binFrames + 1));
    for (std::int64_t done = 0; done < total; done += static_cast<std::int64_t>(binFrames) * kBinsPerRead) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return fail(ErrorCode::Cancelled, "cancelled");
        const int frames = static_cast<int>(std::min<std::int64_t>(static_cast<std::int64_t>(binFrames) * kBinsPerRead, total - done));
        LEC_TRY((*reader)->read(Time::fromSamples(firstSample + done, rate), frames, buffer.data()));
        for (int b = 0; b * binFrames < frames; ++b) {
            float peak = 0;
            const int end = std::min(frames, (b + 1) * binFrames);
            for (int i = b * binFrames * AudioReader::kChannels; i < end * AudioReader::kChannels; ++i) {
                peak = std::max(peak, std::fabs(buffer[static_cast<std::size_t>(i)]));
            }
            out.peaks.push_back(peakToByte(peak));
        }
    }
    return out;
}

}  // namespace lectern::media
