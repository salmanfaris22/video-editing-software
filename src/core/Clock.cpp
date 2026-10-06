#include "core/Clock.h"

#include "core/Rational.h"

#if defined(__APPLE__)
#include <mach/mach_time.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

namespace lectern {

#if defined(__APPLE__)
namespace {
struct Timebase {
    std::int64_t numer = 1;
    std::int64_t denom = 1;
    Timebase() {
        mach_timebase_info_data_t info{};
        if (mach_timebase_info(&info) == KERN_SUCCESS && info.denom != 0) {
            numer = info.numer;
            denom = info.denom;
        }
    }
};
const Timebase& timebase() {
    static const Timebase tb;
    return tb;
}
}  // namespace

std::int64_t HostClock::nativeTicksToNs(std::uint64_t ticks) noexcept {
    const auto& tb = timebase();
    if (tb.numer == tb.denom) return static_cast<std::int64_t>(ticks);
    return mulDiv(static_cast<std::int64_t>(ticks), tb.numer, tb.denom, Rounding::Nearest);
}

std::int64_t HostClock::now() noexcept { return nativeTicksToNs(mach_absolute_time()); }

#elif defined(_WIN32)
namespace {
std::int64_t qpcFrequency() {
    static const std::int64_t freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<std::int64_t>(f.QuadPart);
    }();
    return freq;
}
}  // namespace

std::int64_t HostClock::nativeTicksToNs(std::uint64_t ticks) noexcept {
    return mulDiv(static_cast<std::int64_t>(ticks), 1'000'000'000, qpcFrequency(), Rounding::Nearest);
}

std::int64_t HostClock::now() noexcept {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return nativeTicksToNs(static_cast<std::uint64_t>(c.QuadPart));
}

#else
std::int64_t HostClock::nativeTicksToNs(std::uint64_t ticks) noexcept { return static_cast<std::int64_t>(ticks); }

std::int64_t HostClock::now() noexcept {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
}
#endif

const HostClock& HostClock::shared() noexcept {
    static const HostClock clock;
    return clock;
}

}  // namespace lectern
