#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace lectern {

/// 128-bit UUID (RFC 9562). Stored as two integers so ids are cheap to copy,
/// compare and hash on hot editor paths.
class Uuid {
public:
    constexpr Uuid() noexcept = default;
    constexpr Uuid(std::uint64_t hi, std::uint64_t lo) noexcept : hi_(hi), lo_(lo) {}

    /// Time-ordered UUIDv7 (unix ms + randomness): ids sort by creation time.
    [[nodiscard]] static Uuid generateV7();
    [[nodiscard]] static Uuid generateV4();
    [[nodiscard]] static std::optional<Uuid> parse(std::string_view text);

    [[nodiscard]] std::string toString() const;  // lowercase 8-4-4-4-12
    [[nodiscard]] constexpr bool isNil() const noexcept { return hi_ == 0 && lo_ == 0; }
    [[nodiscard]] constexpr std::uint64_t hi() const noexcept { return hi_; }
    [[nodiscard]] constexpr std::uint64_t lo() const noexcept { return lo_; }

    friend constexpr auto operator<=>(const Uuid&, const Uuid&) noexcept = default;

private:
    std::uint64_t hi_ = 0;
    std::uint64_t lo_ = 0;
};

/// Strongly typed id: ClipId and TrackId cannot be mixed up at compile time.
template <class Tag>
class Id {
public:
    constexpr Id() noexcept = default;
    constexpr explicit Id(Uuid uuid) noexcept : uuid_(uuid) {}

    [[nodiscard]] static Id generate() { return Id(Uuid::generateV7()); }
    [[nodiscard]] static std::optional<Id> parse(std::string_view text) {
        if (auto u = Uuid::parse(text)) return Id(*u);
        return std::nullopt;
    }

    [[nodiscard]] constexpr const Uuid& uuid() const noexcept { return uuid_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return !uuid_.isNil(); }
    [[nodiscard]] std::string toString() const { return uuid_.toString(); }

    friend constexpr auto operator<=>(const Id&, const Id&) noexcept = default;

private:
    Uuid uuid_;
};

}  // namespace lectern

template <>
struct std::hash<lectern::Uuid> {
    std::size_t operator()(const lectern::Uuid& u) const noexcept {
        // UUIDv7 high bits are a timestamp; mix both halves.
        std::uint64_t x = u.hi() ^ (u.lo() * 0x9E3779B97F4A7C15ull);
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdull;
        x ^= x >> 33;
        return static_cast<std::size_t>(x);
    }
};

template <class Tag>
struct std::hash<lectern::Id<Tag>> {
    std::size_t operator()(const lectern::Id<Tag>& id) const noexcept { return std::hash<lectern::Uuid>{}(id.uuid()); }
};
