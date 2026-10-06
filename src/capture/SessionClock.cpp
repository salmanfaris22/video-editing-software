#include "capture/SessionClock.h"

namespace lectern::capture {

std::int64_t SessionClock::start() {
    std::lock_guard lock(mutex_);
    const std::int64_t now = clock_.nowNs();
    if (state_ != State::Idle) return start_.value_or(now);
    start_ = now;
    spans_.push_back(Span{now, kOpen, Time::zero()});
    state_ = State::Running;
    return now;
}

std::int64_t SessionClock::pause() {
    std::lock_guard lock(mutex_);
    const std::int64_t now = clock_.nowNs();
    if (state_ != State::Running) return now;
    Span& last = spans_.back();
    last.hostEnd = now;
    pauses_.push_back(PauseInterval{now, std::nullopt, totalRecordedLocked()});
    state_ = State::Paused;
    return now;
}

std::int64_t SessionClock::resume() {
    std::lock_guard lock(mutex_);
    const std::int64_t now = clock_.nowNs();
    if (state_ != State::Paused) return now;
    const Time sessionAt = totalRecordedLocked();
    spans_.push_back(Span{now, kOpen, sessionAt});
    if (!pauses_.empty()) pauses_.back().hostEnd = now;
    state_ = State::Running;
    return now;
}

std::int64_t SessionClock::stop() {
    std::lock_guard lock(mutex_);
    const std::int64_t now = clock_.nowNs();
    if (state_ == State::Idle || state_ == State::Stopped) return stop_.value_or(now);
    if (state_ == State::Running) spans_.back().hostEnd = now;
    if (state_ == State::Paused && !pauses_.empty() && !pauses_.back().hostEnd) pauses_.back().hostEnd = now;
    stop_ = now;
    state_ = State::Stopped;
    return now;
}

SessionClock::State SessionClock::state() const {
    std::lock_guard lock(mutex_);
    return state_;
}

bool SessionClock::isStarted() const {
    std::lock_guard lock(mutex_);
    return state_ != State::Idle;
}

Time SessionClock::totalRecordedLocked() const {
    Time total;
    for (const Span& s : spans_) {
        if (s.hostEnd == kOpen) continue;
        total += Time::fromNanoseconds(s.hostEnd - s.hostBegin);
    }
    return total;
}

std::optional<Time> SessionClock::toSessionTime(std::int64_t hostNs) const {
    std::lock_guard lock(mutex_);
    // Spans are few (one per resume); scan from the newest, where live data lands.
    for (auto it = spans_.rbegin(); it != spans_.rend(); ++it) {
        if (hostNs >= it->hostBegin && hostNs < it->hostEnd) {
            return it->sessionBegin + Time::fromNanoseconds(hostNs - it->hostBegin);
        }
        if (hostNs >= it->hostEnd) return std::nullopt;  // after this span: paused/stopped gap
    }
    return std::nullopt;
}

Time SessionClock::elapsedAt(std::int64_t hostNs) const {
    std::lock_guard lock(mutex_);
    if (state_ == State::Idle || spans_.empty()) return Time::zero();
    const Span& last = spans_.back();
    if (last.hostEnd == kOpen) {
        const std::int64_t clamped = hostNs > last.hostBegin ? hostNs : last.hostBegin;
        return last.sessionBegin + Time::fromNanoseconds(clamped - last.hostBegin);
    }
    return totalRecordedLocked();
}

std::optional<std::int64_t> SessionClock::startHostNs() const {
    std::lock_guard lock(mutex_);
    return start_;
}

std::optional<std::int64_t> SessionClock::stopHostNs() const {
    std::lock_guard lock(mutex_);
    return stop_;
}

std::optional<Time> SessionClock::stopSessionTime() const {
    std::lock_guard lock(mutex_);
    if (state_ != State::Stopped) return std::nullopt;
    return totalRecordedLocked();
}

std::vector<SessionClock::PauseInterval> SessionClock::pauses() const {
    std::lock_guard lock(mutex_);
    return pauses_;
}

int SessionClock::pauseCount() const {
    std::lock_guard lock(mutex_);
    return static_cast<int>(pauses_.size());
}

}  // namespace lectern::capture
