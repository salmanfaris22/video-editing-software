#include "core/Uuid.h"

#include <chrono>
#include <mutex>
#include <random>

namespace lectern {
namespace {

std::mt19937_64& rng() {
    thread_local std::mt19937_64 engine = [] {
        std::random_device rd;
        std::seed_seq seq{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
        return std::mt19937_64(seq);
    }();
    return engine;
}

int hexValue(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

Uuid Uuid::generateV7() {
    // Monotonic within a process: if two ids share a millisecond, the 12-bit
    // rand_a field is incremented so ids remain strictly ordered.
    static std::mutex mutex;
    static std::uint64_t lastMs = 0;
    static std::uint64_t counter = 0;

    const auto nowMs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
    std::uint64_t ms;
    std::uint64_t randA;
    {
        std::lock_guard lock(mutex);
        if (nowMs > lastMs) {
            lastMs = nowMs;
            counter = rng()() & 0x7FFull;  // leave headroom for increments
        } else {
            ++counter;
            if (counter > 0xFFFull) {  // exhausted: borrow the next millisecond
                ++lastMs;
                counter = 0;
            }
        }
        ms = lastMs;
        randA = counter;
    }
    const std::uint64_t hi = ((ms & 0xFFFFFFFFFFFFull) << 16) | (0x7ull << 12) | (randA & 0xFFFull);
    const std::uint64_t lo = (rng()() & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;  // variant 10
    return Uuid(hi, lo);
}

Uuid Uuid::generateV4() {
    std::uint64_t hi = rng()();
    std::uint64_t lo = rng()();
    hi = (hi & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;
    lo = (lo & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
    return Uuid(hi, lo);
}

std::optional<Uuid> Uuid::parse(std::string_view text) {
    if (text.size() != 36) return std::nullopt;
    std::uint64_t parts[2] = {0, 0};
    int nibble = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return std::nullopt;
            continue;
        }
        const int v = hexValue(c);
        if (v < 0) return std::nullopt;
        std::uint64_t& part = parts[nibble / 16];
        part = (part << 4) | static_cast<std::uint64_t>(v);
        ++nibble;
    }
    return Uuid(parts[0], parts[1]);
}

std::string Uuid::toString() const {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(36, '-');
    std::size_t pos = 0;
    auto emit = [&](std::uint64_t value, int nibbles) {
        for (int i = nibbles - 1; i >= 0; --i) {
            if (pos == 8 || pos == 13 || pos == 18 || pos == 23) ++pos;
            out[pos++] = kHex[(value >> (i * 4)) & 0xF];
        }
    };
    emit(hi_, 16);
    emit(lo_, 16);
    return out;
}

}  // namespace lectern
