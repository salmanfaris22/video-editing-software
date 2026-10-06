#pragma once

#include "core/Time.h"

#include <algorithm>
#include <vector>

namespace lectern::timeline {

struct Vec2 {
    double x = 0;
    double y = 0;
    friend constexpr bool operator==(const Vec2&, const Vec2&) = default;
};

/// Four components; used for crop (left, top, right, bottom fractions).
struct Vec4 {
    double x = 0;
    double y = 0;
    double z = 0;
    double w = 0;
    friend constexpr bool operator==(const Vec4&, const Vec4&) = default;
};

/// Interpolation from a keyframe to the next one (docs/TIMELINE_ENGINE.md §4).
enum class Interpolation { Hold, Linear, EaseIn, EaseOut, EaseInOut, Bezier };

/// Cubic Bézier control points (CSS cubic-bezier semantics). The presets
/// EaseIn/EaseOut/EaseInOut are fixed curves; `Bezier` uses these values.
struct BezierHandles {
    double x1 = 0.42;
    double y1 = 0.0;
    double x2 = 0.58;
    double y2 = 1.0;
    friend constexpr bool operator==(const BezierHandles&, const BezierHandles&) = default;
};

/// Maps linear progress u ∈ [0,1] through the easing curve.
[[nodiscard]] double easedProgress(Interpolation interp, const BezierHandles& handles, double u) noexcept;

template <class T>
struct Keyframe {
    Time time;  ///< clip-local
    T value{};
    Interpolation interp = Interpolation::Linear;
    BezierHandles handles;
    friend bool operator==(const Keyframe&, const Keyframe&) = default;
};

[[nodiscard]] inline double lerp(double a, double b, double u) noexcept { return a + (b - a) * u; }
[[nodiscard]] inline Vec2 lerp(const Vec2& a, const Vec2& b, double u) noexcept {
    return {lerp(a.x, b.x, u), lerp(a.y, b.y, u)};
}
[[nodiscard]] inline Vec4 lerp(const Vec4& a, const Vec4& b, double u) noexcept {
    return {lerp(a.x, b.x, u), lerp(a.y, b.y, u), lerp(a.z, b.z, u), lerp(a.w, b.w, u)};
}

/// A property that is either constant (`value`) or keyframed (`keys`).
template <class T>
struct Animated {
    T value{};
    std::vector<Keyframe<T>> keys;  ///< sorted by time, unique times

    Animated() = default;
    Animated(T v) : value(v) {}  // NOLINT(google-explicit-constructor): constant property

    [[nodiscard]] bool isAnimated() const noexcept { return !keys.empty(); }

    /// Value at clip-local time t.
    [[nodiscard]] T evaluate(Time t) const {
        if (keys.empty()) return value;
        if (t <= keys.front().time) return keys.front().value;
        if (t >= keys.back().time) return keys.back().value;
        const auto it = std::upper_bound(keys.begin(), keys.end(), t,
                                         [](Time lhs, const Keyframe<T>& k) { return lhs < k.time; });
        const Keyframe<T>& b = *it;
        const Keyframe<T>& a = *(it - 1);
        if (a.interp == Interpolation::Hold) return a.value;
        const double span = static_cast<double>((b.time - a.time).ticks());
        const double u = span > 0 ? static_cast<double>((t - a.time).ticks()) / span : 1.0;
        return lerp(a.value, b.value, easedProgress(a.interp, a.handles, u));
    }

    /// Inserts or replaces the key at `t`, keeping keys sorted.
    void setKey(Time t, T v, Interpolation interp = Interpolation::Linear) {
        const auto it = std::lower_bound(keys.begin(), keys.end(), t,
                                         [](const Keyframe<T>& k, Time rhs) { return k.time < rhs; });
        if (it != keys.end() && it->time == t) {
            it->value = v;
            it->interp = interp;
        } else {
            keys.insert(it, Keyframe<T>{t, v, interp, {}});
        }
    }

    friend bool operator==(const Animated&, const Animated&) = default;
};

}  // namespace lectern::timeline
