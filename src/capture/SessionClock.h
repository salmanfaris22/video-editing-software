#pragma once

#include "core/Clock.h"
#include "core/Time.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace lectern::capture {

/// Maps host time to session time for one recording
/// (docs/RECORDING_ENGINE.md §4).
///
///   host   ──T0────────P1══════R1────────S──▶
///   session 0 ─────────a        a────────b
///
/// Session time excludes paused intervals. Data captured before T0, inside a
/// pause, or at/after S has no session time and is dropped. Transitions read
/// the clock while holding the lock, so every query is linearizable with
/// start/pause/resume/stop: any sample captured after a transition's instant
/// is evaluated against the new state.
class SessionClock {
public:
    enum class State { Idle, Running, Paused, Stopped };

    /// A contiguous recorded span of host time.
    struct Span {
        std::int64_t hostBegin = 0;
        std::int64_t hostEnd = 0;  ///< exclusive; INT64_MAX while open
        Time sessionBegin;
    };

    struct PauseInterval {
        std::int64_t hostBegin = 0;
        std::optional<std::int64_t> hostEnd;
        Time sessionTime;  ///< session time at which the pause occurred
    };

    explicit SessionClock(const IClock& clock) : clock_(clock) {}

    /// Starts the session at "now". Returns T0 (host ns).
    std::int64_t start();
    /// Pauses at "now". Returns the pause instant. No-op unless running.
    std::int64_t pause();
    /// Resumes at "now". Returns the resume instant. No-op unless paused.
    std::int64_t resume();
    /// Stops at "now" (also valid while paused). Returns S (host ns).
    std::int64_t stop();

    [[nodiscard]] State state() const;
    [[nodiscard]] bool isStarted() const;

    /// Session time for a capture timestamp, or nullopt if it is not recorded.
    [[nodiscard]] std::optional<Time> toSessionTime(std::int64_t hostNs) const;

    /// Session time "now" (frozen while paused, final after stop, 0 before start).
    [[nodiscard]] Time elapsed() const { return elapsedAt(clock_.nowNs()); }
    [[nodiscard]] Time elapsedAt(std::int64_t hostNs) const;

    /// Visits the recorded spans intersecting [hostBegin, hostEnd), clipped to
    /// that range. Allocation-free; `fn(const Span&)` is called under no lock
    /// with a copy of each span.
    template <class Fn>
    void forEachActiveSpan(std::int64_t hostBegin, std::int64_t hostEnd, Fn&& fn) const;

    [[nodiscard]] std::optional<std::int64_t> startHostNs() const;
    [[nodiscard]] std::optional<std::int64_t> stopHostNs() const;
    /// Total recorded duration once stopped.
    [[nodiscard]] std::optional<Time> stopSessionTime() const;
    [[nodiscard]] std::vector<PauseInterval> pauses() const;
    [[nodiscard]] int pauseCount() const;

    [[nodiscard]] const IClock& clock() const noexcept { return clock_; }

private:
    static constexpr std::int64_t kOpen = INT64_MAX;
    [[nodiscard]] Time totalRecordedLocked() const;

    const IClock& clock_;
    mutable std::mutex mutex_;
    State state_ = State::Idle;
    std::vector<Span> spans_;
    std::vector<PauseInterval> pauses_;
    std::optional<std::int64_t> start_;
    std::optional<std::int64_t> stop_;
};

template <class Fn>
void SessionClock::forEachActiveSpan(std::int64_t hostBegin, std::int64_t hostEnd, Fn&& fn) const {
    // Copy the (tiny) relevant part under the lock, call back without it.
    Span local[8];
    int count = 0;
    {
        std::lock_guard lock(mutex_);
        for (const Span& s : spans_) {
            if (s.hostEnd <= hostBegin || s.hostBegin >= hostEnd) continue;
            Span clipped = s;
            if (clipped.hostBegin < hostBegin) {
                clipped.sessionBegin += Time::fromNanoseconds(hostBegin - clipped.hostBegin);
                clipped.hostBegin = hostBegin;
            }
            if (clipped.hostEnd > hostEnd) clipped.hostEnd = hostEnd;
            if (count < 8) local[count++] = clipped;
        }
    }
    for (int i = 0; i < count; ++i) fn(local[i]);
}

}  // namespace lectern::capture
