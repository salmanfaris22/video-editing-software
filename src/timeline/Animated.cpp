#include "timeline/Animated.h"

#include <cmath>

namespace lectern::timeline {

namespace {

// Cubic Bézier with P0 = (0,0), P3 = (1,1).
double bezierCoord(double p1, double p2, double s) noexcept {
    const double inv = 1.0 - s;
    return 3.0 * inv * inv * s * p1 + 3.0 * inv * s * s * p2 + s * s * s;
}

double bezierDerivative(double p1, double p2, double s) noexcept {
    const double inv = 1.0 - s;
    return 3.0 * inv * inv * p1 + 6.0 * inv * s * (p2 - p1) + 3.0 * s * s * (1.0 - p2);
}

// Solves x(s) = u for s with Newton-Raphson, falling back to bisection when
// the derivative is too flat (guaranteed convergence for x1, x2 ∈ [0, 1]).
double solveCurveX(double x1, double x2, double u) noexcept {
    double s = u;
    for (int i = 0; i < 8; ++i) {
        const double x = bezierCoord(x1, x2, s) - u;
        if (std::fabs(x) < 1e-7) return s;
        const double d = bezierDerivative(x1, x2, s);
        if (std::fabs(d) < 1e-6) break;
        s -= x / d;
    }
    double lo = 0.0;
    double hi = 1.0;
    s = u;
    for (int i = 0; i < 64; ++i) {
        const double x = bezierCoord(x1, x2, s);
        if (std::fabs(x - u) < 1e-7) break;
        if (x < u) {
            lo = s;
        } else {
            hi = s;
        }
        s = 0.5 * (lo + hi);
    }
    return s;
}

double cubicBezier(double x1, double y1, double x2, double y2, double u) noexcept {
    x1 = std::clamp(x1, 0.0, 1.0);
    x2 = std::clamp(x2, 0.0, 1.0);
    return bezierCoord(y1, y2, solveCurveX(x1, x2, u));
}

}  // namespace

double easedProgress(Interpolation interp, const BezierHandles& h, double u) noexcept {
    u = std::clamp(u, 0.0, 1.0);
    switch (interp) {
        case Interpolation::Hold: return 0.0;
        case Interpolation::Linear: return u;
        case Interpolation::EaseIn: return cubicBezier(0.42, 0.0, 1.0, 1.0, u);
        case Interpolation::EaseOut: return cubicBezier(0.0, 0.0, 0.58, 1.0, u);
        case Interpolation::EaseInOut: return cubicBezier(0.42, 0.0, 0.58, 1.0, u);
        case Interpolation::Bezier: return cubicBezier(h.x1, h.y1, h.x2, h.y2, u);
    }
    return u;
}

}  // namespace lectern::timeline
