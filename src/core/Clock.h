#pragma once

#include <atomic>
#include <cstdint>

namespace lectern {

/// Source of "now" in host nanoseconds. Injected everywhere time-dependent
/// decisions are made, so tests can drive the engine with a ManualClock.
class IClock {
public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual std::int64_t nowNs() const noexcept = 0;
};

/// The platform monotonic clock in the same domain capture APIs use for
/// timestamps (docs/RECORDING_ENGINE.md §3):
///   macOS   mach_absolute_time (CMClockGetHostTimeClock), excludes sleep
///   Windows QueryPerformanceCounter
///   Linux   CLOCK_MONOTONIC
class HostClock final : public IClock {
public:
    [[nodiscard]] std::int64_t nowNs() const noexcept override { return now(); }
    [[nodiscard]] static std::int64_t now() noexcept;
    /// Converts native host ticks (mach_absolute_time units / QPC counts) to ns.
    [[nodiscard]] static std::int64_t nativeTicksToNs(std::uint64_t ticks) noexcept;
    [[nodiscard]] static const HostClock& shared() noexcept;
};

/// Deterministic clock for tests and fast-forward simulation.
class ManualClock final : public IClock {
public:
    explicit ManualClock(std::int64_t startNs = 1'000'000'000) noexcept : now_(startNs) {}
    [[nodiscard]] std::int64_t nowNs() const noexcept override { return now_.load(std::memory_order_acquire); }
    void set(std::int64_t ns) noexcept { now_.store(ns, std::memory_order_release); }
    void advance(std::int64_t ns) noexcept { now_.fetch_add(ns, std::memory_order_acq_rel); }

private:
    std::atomic<std::int64_t> now_;
};

}  // namespace lectern
