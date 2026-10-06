#include "core/Time.h"

#include <cmath>
#include <cstdio>

namespace lectern {
namespace {
constexpr std::int64_t kNsPerSecond = 1'000'000'000;
constexpr std::int64_t kUsPerSecond = 1'000'000;
}  // namespace

Time Time::fromMicroseconds(std::int64_t us, Rounding r) noexcept {
    return Time(mulDiv(us, kTicksPerSecond, kUsPerSecond, r));
}

Time Time::fromNanoseconds(std::int64_t ns, Rounding r) noexcept {
    return Time(mulDiv(ns, kTicksPerSecond, kNsPerSecond, r));
}

Time Time::fromRational(std::int64_t value, Rational timeBase, Rounding r) noexcept {
    if (!timeBase.isValid()) return Time(0);
    // value * tb.num / tb.den seconds = value * tb.num * kTicks / tb.den ticks.
    // Reduce kTicks against den first so the intermediate stays small.
    const std::int64_t g = std::gcd(kTicksPerSecond, timeBase.den());
    const std::int64_t ticksNum = (kTicksPerSecond / g) * timeBase.num();
    return Time(mulDiv(value, ticksNum, timeBase.den() / g, r));
}

Time Time::fromSamples(std::int64_t samples, int sampleRate, Rounding r) noexcept {
    if (sampleRate <= 0) return Time(0);
    return Time(mulDiv(samples, kTicksPerSecond, sampleRate, r));
}

Time Time::fromSecondsF(double seconds) noexcept {
    const double ticks = std::round(seconds * static_cast<double>(kTicksPerSecond));
    if (ticks >= static_cast<double>(std::numeric_limits<std::int64_t>::max())) return max();
    if (ticks <= static_cast<double>(std::numeric_limits<std::int64_t>::min())) return min();
    return Time(static_cast<std::int64_t>(ticks));
}

std::int64_t Time::toRational(Rational timeBase, Rounding r) const noexcept {
    if (!timeBase.isValid() || timeBase.num() == 0) return 0;
    // ticks / kTicks seconds / (num/den) = ticks * den / (kTicks * num)
    const std::int64_t g = std::gcd(kTicksPerSecond, timeBase.den());
    return mulDiv(ticks_, timeBase.den() / g, (kTicksPerSecond / g) * timeBase.num(), r);
}

std::int64_t Time::toNanoseconds(Rounding r) const noexcept { return mulDiv(ticks_, kNsPerSecond, kTicksPerSecond, r); }

std::int64_t Time::toMicroseconds(Rounding r) const noexcept { return mulDiv(ticks_, kUsPerSecond, kTicksPerSecond, r); }

std::int64_t Time::toMilliseconds(Rounding r) const noexcept { return mulDiv(ticks_, 1000, kTicksPerSecond, r); }

std::int64_t Time::toSamples(int sampleRate, Rounding r) const noexcept {
    if (sampleRate <= 0) return 0;
    return mulDiv(ticks_, sampleRate, kTicksPerSecond, r);
}

Time Time::scaled(Rational factor, Rounding r) const noexcept {
    if (!factor.isValid()) return Time(0);
    return Time(mulDiv(ticks_, factor.num(), factor.den(), r));
}

std::string Time::toString() const {
    const bool negative = ticks_ < 0;
    const std::int64_t totalMs = (negative ? -*this : *this).toMilliseconds(Rounding::Floor);
    const std::int64_t ms = totalMs % 1000;
    const std::int64_t totalSec = totalMs / 1000;
    const std::int64_t s = totalSec % 60;
    const std::int64_t m = (totalSec / 60) % 60;
    const std::int64_t h = totalSec / 3600;
    char buf[48];
    if (h > 0) {
        std::snprintf(buf, sizeof buf, "%s%lld:%02lld:%02lld.%03lld", negative ? "-" : "", static_cast<long long>(h),
                      static_cast<long long>(m), static_cast<long long>(s), static_cast<long long>(ms));
    } else {
        std::snprintf(buf, sizeof buf, "%s%02lld:%02lld.%03lld", negative ? "-" : "", static_cast<long long>(m),
                      static_cast<long long>(s), static_cast<long long>(ms));
    }
    return buf;
}

// ---------------------------------------------------------------------------

const FrameRate FrameRate::k23_976{24000, 1001};
const FrameRate FrameRate::k24{24, 1};
const FrameRate FrameRate::k25{25, 1};
const FrameRate FrameRate::k29_97{30000, 1001};
const FrameRate FrameRate::k30{30, 1};
const FrameRate FrameRate::k50{50, 1};
const FrameRate FrameRate::k59_94{60000, 1001};
const FrameRate FrameRate::k60{60, 1};

std::int64_t FrameRate::frameIndexAt(Time t, Rounding r) const noexcept {
    if (!isValid()) return 0;
    // frames = ticks * num / (den * kTicks)
    return t.toRational(rate_.inverse(), r);
}

Time FrameRate::frameStart(std::int64_t index) const noexcept {
    if (!isValid()) return Time::zero();
    return Time::fromRational(index, rate_.inverse(), Rounding::Nearest);
}

// ---------------------------------------------------------------------------

std::optional<TimeRange> TimeRange::intersection(const TimeRange& o) const noexcept {
    const Time s = start > o.start ? start : o.start;
    const Time e = end() < o.end() ? end() : o.end();
    if (e <= s) return std::nullopt;
    return TimeRange::fromStartEnd(s, e);
}

std::string formatTimecode(Time t, FrameRate rate) {
    if (!rate.isValid()) return t.toString();
    const bool negative = t.isNegative();
    const Time abs = negative ? -t : t;
    const std::int64_t frame = rate.frameIndexAt(abs, Rounding::Floor);
    // Nominal integer frame count per second (30 for 29.97 non-drop timecode).
    const auto fpsNominal = static_cast<std::int64_t>(std::llround(rate.toDouble()));
    const std::int64_t perSecond = fpsNominal > 0 ? fpsNominal : 1;
    const std::int64_t ff = frame % perSecond;
    const std::int64_t totalSec = frame / perSecond;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%s%02lld:%02lld:%02lld:%02lld", negative ? "-" : "",
                  static_cast<long long>(totalSec / 3600), static_cast<long long>((totalSec / 60) % 60),
                  static_cast<long long>(totalSec % 60), static_cast<long long>(ff));
    return buf;
}

std::string formatDuration(Time t) {
    const std::int64_t total = (t.isNegative() ? -t : t).toMilliseconds(Rounding::Floor) / 1000;
    char buf[32];
    if (total >= 3600) {
        std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", static_cast<long long>(total / 3600),
                      static_cast<long long>((total / 60) % 60), static_cast<long long>(total % 60));
    } else {
        std::snprintf(buf, sizeof buf, "%lld:%02lld", static_cast<long long>(total / 60),
                      static_cast<long long>(total % 60));
    }
    return buf;
}

}  // namespace lectern
