#pragma once

#include <algorithm>
#include <cstdint>

namespace lectern::capture {

/// True when `candidateNs` (a device-reported capture time already converted
/// to the host clock) is believable for a frame that reached us at
/// `arrivalNs`: not in the future, and not older than any real pipeline delay.
[[nodiscard]] constexpr bool plausibleCaptureTime(std::int64_t candidateNs, std::int64_t arrivalNs) noexcept {
    constexpr std::int64_t kFutureToleranceNs = 5'000'000;  // 5 ms of clock-read jitter
    constexpr std::int64_t kMaxDelayNs = 1'000'000'000;     // 1 s
    return candidateNs <= arrivalNs + kFutureToleranceNs && candidateNs >= arrivalNs - kMaxDelayNs;
}

/// Maps device media timestamps with an unknown epoch onto the host clock.
///
/// host = media + offset, where offset follows the smallest observed
/// (arrival − media): the least-delayed delivery. Jitter in delivery
/// therefore never reaches the output; the result trails the true capture
/// time by the pipeline's minimum latency. The offset leaks upward a little
/// per sample so device-clock drift (and an early outlier) heal, and it
/// re-anchors when the media timeline jumps (device restart, discontinuity).
class ArrivalAnchoredClock {
public:
    explicit ArrivalAnchoredClock(std::int64_t leakNsPerSample = 20'000,
                                  std::int64_t jumpThresholdNs = 1'000'000'000) noexcept
        : leak_(leakNsPerSample), jumpThreshold_(jumpThresholdNs) {}

    [[nodiscard]] std::int64_t map(std::int64_t mediaNs, std::int64_t arrivalNs) noexcept {
        const std::int64_t candidate = arrivalNs - mediaNs;
        const bool jumped = anchored_ && (mediaNs < lastMediaNs_ || candidate - offset_ > jumpThreshold_ ||
                                          offset_ - candidate > jumpThreshold_);
        if (!anchored_ || jumped) {
            offset_ = candidate;
            anchored_ = true;
            if (jumped) ++reanchors_;
        } else {
            offset_ = std::min(offset_ + leak_, candidate);
        }
        lastMediaNs_ = mediaNs;
        return mediaNs + offset_;
    }

    void reset() noexcept { anchored_ = false; }
    [[nodiscard]] std::int64_t offsetNs() const noexcept { return offset_; }
    [[nodiscard]] std::uint64_t reanchors() const noexcept { return reanchors_; }

private:
    std::int64_t leak_;
    std::int64_t jumpThreshold_;
    bool anchored_ = false;
    std::int64_t offset_ = 0;
    std::int64_t lastMediaNs_ = 0;
    std::uint64_t reanchors_ = 0;
};

}  // namespace lectern::capture
