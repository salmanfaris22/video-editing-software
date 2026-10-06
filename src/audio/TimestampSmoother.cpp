#include "audio/TimestampSmoother.h"

#include <cmath>

namespace lectern::audio {

TimestampSmoother::TimestampSmoother(const Config& config)
    : config_(config), nominalSlope_(1e9 / config.nominalRate), slope_(nominalSlope_) {}

void TimestampSmoother::reset() noexcept {
    points_.clear();
    anchored_ = false;
    slope_ = nominalSlope_;
    intercept_ = 0;
}

double TimestampSmoother::hostTimeOf(double sampleIndex) const noexcept {
    if (!anchored_) return 0;
    const double x = sampleIndex - static_cast<double>(anchorIndex_);
    return static_cast<double>(anchorHost_) + intercept_ + slope_ * x;
}

double TimestampSmoother::driftPpm() const noexcept { return (nominalSlope_ / slope_ - 1.0) * 1e6; }

void TimestampSmoother::refit() noexcept {
    const std::size_t n = points_.size();
    if (n < config_.minPointsForFit) {
        // Too few points for a stable slope: keep the current slope (nominal or
        // the pre-discontinuity estimate) and anchor through the mean point.
        double my = 0;
        double mx = 0;
        for (const auto& p : points_) {
            mx += p.x;
            my += p.y;
        }
        mx /= static_cast<double>(n);
        my /= static_cast<double>(n);
        intercept_ = my - slope_ * mx;
        return;
    }
    // Two-pass, mean-centered least squares: numerically stable even with
    // large indices (cancellation-free compared with running Σx² sums).
    double mx = 0;
    double my = 0;
    for (const auto& p : points_) {
        mx += p.x;
        my += p.y;
    }
    mx /= static_cast<double>(n);
    my /= static_cast<double>(n);
    double sxx = 0;
    double sxy = 0;
    for (const auto& p : points_) {
        const double dx = p.x - mx;
        sxx += dx * dx;
        sxy += dx * (p.y - my);
    }
    if (sxx > 0) {
        const double b = sxy / sxx;
        // Physical sanity bound: no real device is off by more than 2 %.
        if (b > nominalSlope_ * 0.98 && b < nominalSlope_ * 1.02) slope_ = b;
    }
    intercept_ = my - slope_ * mx;
}

TimestampSmoother::Update TimestampSmoother::add(std::int64_t sampleIndex, std::int64_t hostNs) {
    Update u;
    if (!anchored_) {
        anchored_ = true;
        anchorIndex_ = sampleIndex;
        anchorHost_ = hostNs;
        points_.push_back({0.0, 0.0});
        intercept_ = 0;
        u.smoothedHostNs = static_cast<double>(hostNs);
        return u;
    }

    const double predicted = hostTimeOf(static_cast<double>(sampleIndex));
    u.residualNs = static_cast<double>(hostNs) - predicted;
    if (std::fabs(u.residualNs) > config_.discontinuityThresholdNs) {
        // Re-anchor at the new point; keep the slope as the prior.
        ++discontinuities_;
        u.discontinuity = true;
        points_.clear();
        anchorIndex_ = sampleIndex;
        anchorHost_ = hostNs;
        intercept_ = 0;
        points_.push_back({0.0, 0.0});
        u.smoothedHostNs = static_cast<double>(hostNs);
        return u;
    }

    points_.push_back({static_cast<double>(sampleIndex - anchorIndex_), static_cast<double>(hostNs - anchorHost_)});
    const double windowNs = config_.windowSeconds * 1e9;
    while (points_.size() > config_.maxPoints || (points_.size() > 2 && points_.back().y - points_.front().y > windowNs)) {
        points_.pop_front();
    }
    refit();
    u.smoothedHostNs = hostTimeOf(static_cast<double>(sampleIndex));
    return u;
}

}  // namespace lectern::audio
