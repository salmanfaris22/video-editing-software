#pragma once

#include <compare>
#include <cstdint>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>

namespace lectern {

/// Rounding used by every integer time conversion. There is no implicit
/// rounding anywhere in the time math: callers always say what they want.
enum class Rounding : std::uint8_t {
    Floor,       ///< toward negative infinity
    Ceil,        ///< toward positive infinity
    Nearest,     ///< half away from zero
    TowardZero,  ///< truncate
};

/// Computes a * b / c exactly (128-bit intermediate) with the given rounding.
/// Preconditions: c > 0. Saturates to INT64_MIN / INT64_MAX on overflow.
[[nodiscard]] std::int64_t mulDiv(std::int64_t a, std::int64_t b, std::int64_t c, Rounding r) noexcept;

namespace detail {
/// Portable implementation used when the compiler has no __int128 (MSVC).
/// Exposed so tests can verify it against the native implementation.
[[nodiscard]] std::int64_t mulDivPortable(std::int64_t a, std::int64_t b, std::int64_t c, Rounding r) noexcept;
}  // namespace detail

/// Exact rational number with a positive denominator, always reduced.
/// A zero denominator marks an invalid/unknown value (e.g. unknown frame rate).
class Rational {
public:
    constexpr Rational() noexcept = default;
    constexpr Rational(std::int64_t num, std::int64_t den = 1) noexcept : num_(num), den_(den) { normalize(); }

    [[nodiscard]] constexpr std::int64_t num() const noexcept { return num_; }
    [[nodiscard]] constexpr std::int64_t den() const noexcept { return den_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return den_ != 0; }
    [[nodiscard]] constexpr bool isZero() const noexcept { return num_ == 0 && den_ != 0; }
    [[nodiscard]] constexpr bool isPositive() const noexcept { return den_ != 0 && num_ > 0; }

    [[nodiscard]] constexpr Rational inverse() const noexcept { return {den_, num_}; }
    [[nodiscard]] double toDouble() const noexcept {
        return den_ == 0 ? 0.0 : static_cast<double>(num_) / static_cast<double>(den_);
    }

    /// Multiplies an integer by this rational: value * num / den.
    [[nodiscard]] std::int64_t scale(std::int64_t value, Rounding r) const noexcept { return mulDiv(value, num_, den_, r); }

    [[nodiscard]] std::string toString() const;  // "30000/1001"
    [[nodiscard]] static std::optional<Rational> parse(std::string_view text);  // "30000/1001" or "30"

    /// Best rational approximation of a double with bounded denominator
    /// (continued fractions). Used only at UI boundaries.
    [[nodiscard]] static Rational fromDouble(double value, std::int64_t maxDen = 1'000'000);

    friend constexpr bool operator==(const Rational& a, const Rational& b) noexcept {
        return a.num_ == b.num_ && a.den_ == b.den_;
    }
    friend std::strong_ordering operator<=>(const Rational& a, const Rational& b) noexcept;

    friend Rational operator*(const Rational& a, const Rational& b) noexcept;
    friend Rational operator/(const Rational& a, const Rational& b) noexcept;

private:
    constexpr void normalize() noexcept {
        if (den_ == 0) {
            num_ = 0;
            return;
        }
        if (den_ < 0) {
            num_ = -num_;
            den_ = -den_;
        }
        const std::int64_t g = std::gcd(num_ < 0 ? -num_ : num_, den_);
        if (g > 1) {
            num_ /= g;
            den_ /= g;
        }
    }

    std::int64_t num_ = 0;
    std::int64_t den_ = 1;
};

}  // namespace lectern
