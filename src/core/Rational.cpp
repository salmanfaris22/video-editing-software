#include "core/Rational.h"

#include <charconv>
#include <cmath>
#include <limits>

namespace lectern {
namespace {

constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();

// Unsigned 128-bit helpers for the portable path.
struct U128 {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;
};

U128 mul64(std::uint64_t a, std::uint64_t b) noexcept {
    const std::uint64_t aLo = a & 0xFFFFFFFFull;
    const std::uint64_t aHi = a >> 32;
    const std::uint64_t bLo = b & 0xFFFFFFFFull;
    const std::uint64_t bHi = b >> 32;
    const std::uint64_t p0 = aLo * bLo;
    const std::uint64_t p1 = aLo * bHi;
    const std::uint64_t p2 = aHi * bLo;
    const std::uint64_t p3 = aHi * bHi;
    const std::uint64_t mid = (p0 >> 32) + (p1 & 0xFFFFFFFFull) + (p2 & 0xFFFFFFFFull);
    U128 r;
    r.lo = (p0 & 0xFFFFFFFFull) | (mid << 32);
    r.hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
    return r;
}

// Divides a 128-bit value by a 64-bit divisor (shift-subtract). Returns the
// quotient (saturated flag set if it does not fit in 64 bits) and remainder.
struct DivResult {
    std::uint64_t quotient = 0;
    std::uint64_t remainder = 0;
    bool overflow = false;
};

DivResult div128by64(U128 n, std::uint64_t d) noexcept {
    DivResult res;
    if (n.hi >= d) {  // quotient would not fit in 64 bits
        res.overflow = true;
        return res;
    }
    std::uint64_t rem = n.hi;
    std::uint64_t q = 0;
    for (int i = 63; i >= 0; --i) {
        const bool carry = (rem >> 63) != 0;
        rem = (rem << 1) | ((n.lo >> i) & 1u);
        q <<= 1;
        if (carry || rem >= d) {
            rem -= d;
            q |= 1u;
        }
    }
    res.quotient = q;
    res.remainder = rem;
    return res;
}

// |a*b| / c with rounding direction chosen for a non-negative result.
// roundUp: add (c-1) before dividing; half: add c/2.
std::int64_t finish(bool negative, std::uint64_t q, std::uint64_t rem, std::uint64_t c, Rounding r) noexcept {
    bool bump = false;
    if (rem != 0) {
        switch (r) {
            case Rounding::TowardZero:
                bump = false;
                break;
            case Rounding::Floor:
                bump = negative;  // magnitude grows when result is negative
                break;
            case Rounding::Ceil:
                bump = !negative;
                break;
            case Rounding::Nearest:
                bump = rem >= c - rem;  // rem >= c/2 (half away from zero)
                break;
        }
    }
    if (bump) {
        if (q == std::numeric_limits<std::uint64_t>::max()) return negative ? kMin : kMax;
        ++q;
    }
    if (negative) {
        if (q > static_cast<std::uint64_t>(kMax) + 1u) return kMin;
        return q == static_cast<std::uint64_t>(kMax) + 1u ? kMin : -static_cast<std::int64_t>(q);
    }
    if (q > static_cast<std::uint64_t>(kMax)) return kMax;
    return static_cast<std::int64_t>(q);
}

std::uint64_t magnitude(std::int64_t v) noexcept {
    return v < 0 ? (~static_cast<std::uint64_t>(v) + 1u) : static_cast<std::uint64_t>(v);
}

[[maybe_unused]] bool lessU128(U128 a, U128 b) noexcept { return a.hi < b.hi || (a.hi == b.hi && a.lo < b.lo); }

}  // namespace

namespace detail {

std::int64_t mulDivPortable(std::int64_t a, std::int64_t b, std::int64_t c, Rounding r) noexcept {
    if (c <= 0) return 0;
    const bool negative = (a < 0) != (b < 0) && a != 0 && b != 0;
    const U128 product = mul64(magnitude(a), magnitude(b));
    const auto uc = static_cast<std::uint64_t>(c);
    const DivResult d = div128by64(product, uc);
    if (d.overflow) return negative ? kMin : kMax;
    return finish(negative, d.quotient, d.remainder, uc, r);
}

}  // namespace detail

std::int64_t mulDiv(std::int64_t a, std::int64_t b, std::int64_t c, Rounding r) noexcept {
#if defined(__SIZEOF_INT128__)
    if (c <= 0) return 0;
    const bool negative = (a < 0) != (b < 0) && a != 0 && b != 0;
    const unsigned __int128 product =
        static_cast<unsigned __int128>(magnitude(a)) * static_cast<unsigned __int128>(magnitude(b));
    const auto uc = static_cast<unsigned __int128>(c);
    const unsigned __int128 q = product / uc;
    const auto rem = static_cast<std::uint64_t>(product % uc);
    if ((q >> 64) != 0) return negative ? kMin : kMax;
    return finish(negative, static_cast<std::uint64_t>(q), rem, static_cast<std::uint64_t>(c), r);
#else
    return detail::mulDivPortable(a, b, c, r);
#endif
}

std::strong_ordering operator<=>(const Rational& a, const Rational& b) noexcept {
    // Invalid values sort first and compare equal to each other.
    if (!a.isValid() || !b.isValid()) {
        if (a.isValid() == b.isValid()) return std::strong_ordering::equal;
        return a.isValid() ? std::strong_ordering::greater : std::strong_ordering::less;
    }
#if defined(__SIZEOF_INT128__)
    const __int128 lhs = static_cast<__int128>(a.num()) * b.den();
    const __int128 rhs = static_cast<__int128>(b.num()) * a.den();
    return lhs <=> rhs;
#else
    // Exact comparison without __int128: compare signs, then magnitudes of the
    // cross products as unsigned 128-bit values.
    const int sa = (a.num() > 0) - (a.num() < 0);
    const int sb = (b.num() > 0) - (b.num() < 0);
    if (sa != sb) return sa < sb ? std::strong_ordering::less : std::strong_ordering::greater;
    if (sa == 0) return std::strong_ordering::equal;
    const U128 lhs = mul64(magnitude(a.num()), static_cast<std::uint64_t>(b.den()));
    const U128 rhs = mul64(magnitude(b.num()), static_cast<std::uint64_t>(a.den()));
    if (lhs.hi == rhs.hi && lhs.lo == rhs.lo) return std::strong_ordering::equal;
    const bool magLess = lessU128(lhs, rhs);
    return (magLess == (sa > 0)) ? std::strong_ordering::less : std::strong_ordering::greater;
#endif
}

Rational operator*(const Rational& a, const Rational& b) noexcept {
    if (!a.isValid() || !b.isValid()) return Rational(0, 0);
    // Cross-reduce first to limit overflow.
    const std::int64_t g1 = std::gcd(a.num() < 0 ? -a.num() : a.num(), b.den());
    const std::int64_t g2 = std::gcd(b.num() < 0 ? -b.num() : b.num(), a.den());
    const std::int64_t n1 = g1 ? a.num() / g1 : a.num();
    const std::int64_t d2 = g1 ? b.den() / g1 : b.den();
    const std::int64_t n2 = g2 ? b.num() / g2 : b.num();
    const std::int64_t d1 = g2 ? a.den() / g2 : a.den();
    return Rational(n1 * n2, d1 * d2);
}

Rational operator/(const Rational& a, const Rational& b) noexcept {
    if (b.num() == 0) return Rational(0, 0);
    return a * b.inverse();
}

std::string Rational::toString() const {
    return std::to_string(num_) + "/" + std::to_string(den_);
}

std::optional<Rational> Rational::parse(std::string_view text) {
    auto parseInt = [](std::string_view s, std::int64_t& out) {
        const auto* first = s.data();
        const auto* last = s.data() + s.size();
        auto [ptr, ec] = std::from_chars(first, last, out);
        return ec == std::errc{} && ptr == last;
    };
    std::int64_t num = 0;
    std::int64_t den = 1;
    if (const auto slash = text.find('/'); slash != std::string_view::npos) {
        if (!parseInt(text.substr(0, slash), num) || !parseInt(text.substr(slash + 1), den) || den == 0) {
            return std::nullopt;
        }
    } else if (!parseInt(text, num)) {
        return std::nullopt;
    }
    return Rational(num, den);
}

Rational Rational::fromDouble(double value, std::int64_t maxDen) {
    if (!std::isfinite(value)) return Rational(0, 0);
    const bool negative = value < 0;
    double x = std::fabs(value);
    // Continued fraction expansion with convergents h/k.
    std::int64_t h0 = 0, h1 = 1, k0 = 1, k1 = 0;
    for (int i = 0; i < 64; ++i) {
        const double a = std::floor(x);
        if (a > 9.0e15) break;
        const auto ai = static_cast<std::int64_t>(a);
        const std::int64_t h2 = ai * h1 + h0;
        const std::int64_t k2 = ai * k1 + k0;
        if (k2 > maxDen) break;
        h0 = h1;
        h1 = h2;
        k0 = k1;
        k1 = k2;
        const double frac = x - a;
        if (frac < 1e-12) break;
        x = 1.0 / frac;
    }
    if (k1 == 0) return Rational(0, 1);
    return Rational(negative ? -h1 : h1, k1);
}

}  // namespace lectern
