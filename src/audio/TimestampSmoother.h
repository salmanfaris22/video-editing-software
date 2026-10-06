#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

namespace lectern::audio {

/// Estimates the device sample clock against the host clock
/// (docs/RECORDING_ENGINE.md §6.1).
///
/// Each audio chunk reports (input sample index of its first sample, host
/// capture time). The smoother fits `hostNs = a + b·index` by least squares
/// over a sliding window, which removes callback timestamp jitter and measures
/// the device's true rate (drift in ppm). A residual above the threshold is a
/// discontinuity (lost samples, device reset, system sleep): the fit re-anchors
/// at the new point and keeps the previous slope as its prior.
class TimestampSmoother {
public:
    struct Config {
        double nominalRate = 48'000.0;
        double windowSeconds = 20.0;
        double discontinuityThresholdNs = 20e6;  ///< 20 ms
        std::size_t maxPoints = 2'048;
        std::size_t minPointsForFit = 8;
    };

    struct Update {
        double smoothedHostNs = 0;  ///< fitted host time of the chunk's first sample
        bool discontinuity = false;
        double residualNs = 0;      ///< measured − predicted before this point was added
    };

    explicit TimestampSmoother(const Config& config);

    Update add(std::int64_t sampleIndex, std::int64_t hostNs);

    /// Fitted host time (ns) of any input sample index; extrapolates.
    [[nodiscard]] double hostTimeOf(double sampleIndex) const noexcept;
    /// Fitted nanoseconds per input sample.
    [[nodiscard]] double samplePeriodNs() const noexcept { return slope_; }
    /// Device rate deviation from nominal: (actual/nominal − 1) × 1e6.
    [[nodiscard]] double driftPpm() const noexcept;
    [[nodiscard]] std::size_t pointCount() const noexcept { return points_.size(); }
    [[nodiscard]] std::uint64_t discontinuities() const noexcept { return discontinuities_; }
    void reset() noexcept;

private:
    struct Point {
        double x;  // sample index relative to anchor
        double y;  // host ns relative to anchor
    };
    void refit() noexcept;

    Config config_;
    std::deque<Point> points_;
    std::int64_t anchorIndex_ = 0;
    std::int64_t anchorHost_ = 0;
    bool anchored_ = false;
    double nominalSlope_;
    double slope_;
    double intercept_ = 0;  // relative to anchor
    std::uint64_t discontinuities_ = 0;
};

}  // namespace lectern::audio
