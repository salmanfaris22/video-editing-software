#pragma once

#include "core/Rational.h"

#include <compare>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace lectern {

/// A point or span on a timeline, in integer ticks.
///
/// 705,600,000 ticks per second ("flicks") divides evenly by every common
/// frame duration (24, 25, 30, 48, 50, 60, 90, 100, 120 fps and the NTSC
/// x/1001 rates) and every common audio sample period (8 kHz … 192 kHz,
/// including 44.1 kHz). Frame and sample boundaries are therefore exact
/// integers. Range: ±414 years. See docs/TIMELINE_ENGINE.md §1.
class Time {
public:
    static constexpr std::int64_t kTicksPerSecond = 705'600'000;
    static constexpr std::int64_t kTicksPerMillisecond = kTicksPerSecond / 1000;

    constexpr Time() noexcept = default;

    [[nodiscard]] static constexpr Time fromTicks(std::int64_t ticks) noexcept { return Time(ticks); }
    [[nodiscard]] static constexpr Time zero() noexcept { return Time(0); }
    [[nodiscard]] static constexpr Time max() noexcept { return Time(std::numeric_limits<std::int64_t>::max()); }
    [[nodiscard]] static constexpr Time min() noexcept { return Time(std::numeric_limits<std::int64_t>::min()); }

    [[nodiscard]] static constexpr Time fromSeconds(std::int64_t s) noexcept { return Time(s * kTicksPerSecond); }
    [[nodiscard]] static constexpr Time fromMilliseconds(std::int64_t ms) noexcept {
        return Time(ms * kTicksPerMillisecond);
    }
    [[nodiscard]] static Time fromMicroseconds(std::int64_t us, Rounding r = Rounding::Nearest) noexcept;
    [[nodiscard]] static Time fromNanoseconds(std::int64_t ns, Rounding r = Rounding::Nearest) noexcept;
    /// value expressed in units of `timeBase` seconds (e.g. pts with 1/90000).
    [[nodiscard]] static Time fromRational(std::int64_t value, Rational timeBase,
                                           Rounding r = Rounding::Nearest) noexcept;
    [[nodiscard]] static Time fromSamples(std::int64_t samples, int sampleRate,
                                          Rounding r = Rounding::Nearest) noexcept;
    /// UI boundary only. Never use floating point seconds in engine math.
    [[nodiscard]] static Time fromSecondsF(double seconds) noexcept;

    [[nodiscard]] constexpr std::int64_t ticks() const noexcept { return ticks_; }
    [[nodiscard]] std::int64_t toRational(Rational timeBase, Rounding r = Rounding::Nearest) const noexcept;
    [[nodiscard]] std::int64_t toNanoseconds(Rounding r = Rounding::Nearest) const noexcept;
    [[nodiscard]] std::int64_t toMicroseconds(Rounding r = Rounding::Nearest) const noexcept;
    [[nodiscard]] std::int64_t toMilliseconds(Rounding r = Rounding::Nearest) const noexcept;
    [[nodiscard]] std::int64_t toSamples(int sampleRate, Rounding r = Rounding::Nearest) const noexcept;
    [[nodiscard]] double toSecondsF() const noexcept {
        return static_cast<double>(ticks_) / static_cast<double>(kTicksPerSecond);
    }

    /// Multiplies by an exact rational factor (speed changes, rate mapping).
    [[nodiscard]] Time scaled(Rational factor, Rounding r = Rounding::Nearest) const noexcept;

    [[nodiscard]] constexpr bool isZero() const noexcept { return ticks_ == 0; }
    [[nodiscard]] constexpr bool isNegative() const noexcept { return ticks_ < 0; }

    constexpr Time& operator+=(Time o) noexcept { ticks_ += o.ticks_; return *this; }
    constexpr Time& operator-=(Time o) noexcept { ticks_ -= o.ticks_; return *this; }
    [[nodiscard]] friend constexpr Time operator+(Time a, Time b) noexcept { return Time(a.ticks_ + b.ticks_); }
    [[nodiscard]] friend constexpr Time operator-(Time a, Time b) noexcept { return Time(a.ticks_ - b.ticks_); }
    [[nodiscard]] friend constexpr Time operator-(Time a) noexcept { return Time(-a.ticks_); }
    [[nodiscard]] friend constexpr Time operator*(Time a, std::int64_t k) noexcept { return Time(a.ticks_ * k); }
    [[nodiscard]] friend constexpr Time operator*(std::int64_t k, Time a) noexcept { return Time(a.ticks_ * k); }
    friend constexpr auto operator<=>(const Time&, const Time&) noexcept = default;

    /// "HH:MM:SS.mmm" (or "MM:SS.mmm" below one hour) for logs and debugging.
    [[nodiscard]] std::string toString() const;

private:
    constexpr explicit Time(std::int64_t ticks) noexcept : ticks_(ticks) {}
    std::int64_t ticks_ = 0;
};

/// A frame rate as an exact rational (e.g. 30000/1001).
class FrameRate {
public:
    constexpr FrameRate() noexcept = default;
    constexpr FrameRate(std::int64_t num, std::int64_t den = 1) noexcept : rate_(num, den) {}
    constexpr explicit FrameRate(Rational rate) noexcept : rate_(rate) {}

    [[nodiscard]] constexpr Rational rational() const noexcept { return rate_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return rate_.isPositive(); }
    /// Time base of one frame (den/num seconds), e.g. 1001/30000.
    [[nodiscard]] constexpr Rational frameTimeBase() const noexcept { return rate_.inverse(); }
    [[nodiscard]] double toDouble() const noexcept { return rate_.toDouble(); }

    /// Duration of one frame. Exact for every rate listed on `Time`.
    [[nodiscard]] Time frameDuration() const noexcept { return frameStart(1); }
    /// Index of the frame containing `t` (Floor), or nearest boundary (Nearest).
    [[nodiscard]] std::int64_t frameIndexAt(Time t, Rounding r = Rounding::Floor) const noexcept;
    /// Start time of frame `index`.
    [[nodiscard]] Time frameStart(std::int64_t index) const noexcept;
    /// Snaps a time onto the frame grid.
    [[nodiscard]] Time snap(Time t, Rounding r = Rounding::Nearest) const noexcept {
        return frameStart(frameIndexAt(t, r));
    }

    friend constexpr bool operator==(const FrameRate&, const FrameRate&) noexcept = default;

    static const FrameRate k23_976;
    static const FrameRate k24;
    static const FrameRate k25;
    static const FrameRate k29_97;
    static const FrameRate k30;
    static const FrameRate k50;
    static const FrameRate k59_94;
    static const FrameRate k60;

private:
    Rational rate_{0, 0};
};

/// Half-open interval [start, start + duration).
struct TimeRange {
    Time start;
    Time duration;

    [[nodiscard]] static constexpr TimeRange fromStartEnd(Time s, Time e) noexcept { return {s, e - s}; }
    [[nodiscard]] constexpr Time end() const noexcept { return start + duration; }
    [[nodiscard]] constexpr bool isEmpty() const noexcept { return duration <= Time::zero(); }
    [[nodiscard]] constexpr bool contains(Time t) const noexcept { return t >= start && t < end(); }
    [[nodiscard]] constexpr bool contains(const TimeRange& o) const noexcept {
        return o.start >= start && o.end() <= end();
    }
    [[nodiscard]] constexpr bool intersects(const TimeRange& o) const noexcept {
        return start < o.end() && o.start < end();
    }
    [[nodiscard]] std::optional<TimeRange> intersection(const TimeRange& o) const noexcept;

    friend constexpr bool operator==(const TimeRange&, const TimeRange&) noexcept = default;
};

/// "HH:MM:SS:FF" timecode on the given frame grid.
[[nodiscard]] std::string formatTimecode(Time t, FrameRate rate);
/// "M:SS" or "H:MM:SS" for recording timers and UI durations.
[[nodiscard]] std::string formatDuration(Time t);

}  // namespace lectern
