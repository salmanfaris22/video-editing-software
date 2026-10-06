#pragma once

#include "core/Time.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace lectern::capture {

/// Converts variable-rate captured frames into constant-frame-rate output
/// slots (docs/RECORDING_ENGINE.md §5.2). Pure logic, independent of FFmpeg,
/// so it is unit-tested with plain integers as frames.
///
/// Slot k covers session time k·D. A frame captured at t belongs to slot
/// round(t/D). Frames arrive in capture order, so a frame for slot s closes
/// every slot before s; with no frames, slots close when
/// now > end(slot) + latencyAllowance. Closed slots are emitted with their
/// frame, or with a duplicate of the last emitted frame (bounded by maxHold),
/// or skipped (gap) when nothing recent exists.
///
/// The pacer holds at most two frames (pending + last), which matters for
/// capture APIs with small surface pools.
///
/// `Dup` creates an extra reference to a frame (av_frame_ref for AVFrames).
template <class FrameT, class Dup>
class VideoPacer {
public:
    struct Config {
        FrameRate frameRate{30, 1};
        Time latencyAllowance = Time::fromMilliseconds(100);
        std::optional<Time> maxHold;  ///< nullopt = duplicate indefinitely (screens)
    };

    struct Counters {
        std::uint64_t emitted = 0;     ///< slots emitted (real + duplicates)
        std::uint64_t real = 0;        ///< slots filled with a fresh frame
        std::uint64_t duplicated = 0;  ///< slots filled by repeating the last frame
        std::uint64_t decimated = 0;   ///< frames replaced by a newer frame in the same slot
        std::uint64_t late = 0;        ///< frames whose slot was already emitted
        std::uint64_t gapSlots = 0;    ///< slots left empty (maxHold exceeded / nothing yet)
    };

    VideoPacer(const Config& config, Dup dup) : config_(config), dup_(std::move(dup)) {
        frameDuration_ = config_.frameRate.frameDuration();
    }

    /// Provides the newest pre-roll frame so slot 0 is filled even if the
    /// source is idle at T0 (static screen).
    void seed(FrameT frame, Time captureTime) {
        last_ = std::move(frame);
        lastRealTime_ = captureTime;
    }

    /// Requests that the next emitted slot be a keyframe (after resume).
    void requestKeyframe() noexcept { keyframeRequested_ = true; }

    /// Emit signature: emit(std::int64_t slot, FrameT&& frame, bool duplicate, bool keyframe)
    template <class Emit>
    void push(FrameT frame, Time captureTime, Emit&& emit) {
        if (finished_) return;
        const std::int64_t slot = slotOf(captureTime);
        if (slot < nextSlot_) {
            // Too late for its slot; still the freshest content for duplicates.
            ++counters_.late;
            last_ = std::move(frame);
            lastRealTime_ = captureTime;
            return;
        }
        closeUpTo(slot, emit);
        if (pending_) {
            ++counters_.decimated;  // same slot: newer frame wins
        }
        pending_ = Pending{slot, std::move(frame), captureTime};
    }

    /// Closes slots whose acceptance window has passed.
    template <class Emit>
    void advance(Time now, Emit&& emit) {
        if (finished_) return;
        while (slotEnd(nextSlot_) + config_.latencyAllowance <= now) emitSlot(nextSlot_++, emit);
    }

    /// Emits every slot that starts before `end` and stops accepting frames.
    template <class Emit>
    void finish(Time end, Emit&& emit) {
        if (finished_) return;
        const std::int64_t count = config_.frameRate.frameIndexAt(end, Rounding::Ceil);
        if (pending_ && pending_->slot >= count) {
            // Captured just before the stop but rounds past the last slot:
            // use it as the freshest content for the final slots.
            last_ = std::move(pending_->frame);
            lastRealTime_ = pending_->time;
            pending_.reset();
        }
        closeUpTo(count, emit);
        finished_ = true;
    }

    [[nodiscard]] std::int64_t nextSlot() const noexcept { return nextSlot_; }
    [[nodiscard]] const Counters& counters() const noexcept { return counters_; }
    [[nodiscard]] Time frameDuration() const noexcept { return frameDuration_; }
    [[nodiscard]] Time slotTime(std::int64_t slot) const { return config_.frameRate.frameStart(slot); }

private:
    struct Pending {
        std::int64_t slot;
        FrameT frame;
        Time time;
    };

    [[nodiscard]] std::int64_t slotOf(Time t) const { return config_.frameRate.frameIndexAt(t, Rounding::Nearest); }
    /// Frames with capture time < slotEnd(k) round to a slot ≤ k.
    [[nodiscard]] Time slotEnd(std::int64_t slot) const {
        return config_.frameRate.frameStart(slot) +
               Time::fromTicks((frameDuration_.ticks() + 1) / 2);
    }

    template <class Emit>
    void closeUpTo(std::int64_t limit, Emit& emit) {
        while (nextSlot_ < limit) emitSlot(nextSlot_++, emit);
    }

    template <class Emit>
    void emitSlot(std::int64_t slot, Emit& emit) {
        const bool key = keyframeRequested_;
        if (pending_ && pending_->slot == slot) {
            last_ = dup_(pending_->frame);
            lastRealTime_ = pending_->time;
            FrameT frame = std::move(pending_->frame);
            pending_.reset();
            ++counters_.emitted;
            ++counters_.real;
            keyframeRequested_ = false;
            emit(slot, std::move(frame), false, key);
            return;
        }
        if (last_ && (!config_.maxHold || slotTime(slot) - lastRealTime_ <= *config_.maxHold)) {
            ++counters_.emitted;
            ++counters_.duplicated;
            keyframeRequested_ = false;
            emit(slot, dup_(*last_), true, key);
            return;
        }
        ++counters_.gapSlots;
    }

    Config config_;
    Dup dup_;
    Time frameDuration_;
    std::int64_t nextSlot_ = 0;
    std::optional<Pending> pending_;
    std::optional<FrameT> last_;
    Time lastRealTime_ = Time::min();
    Counters counters_;
    bool keyframeRequested_ = false;
    bool finished_ = false;
};

}  // namespace lectern::capture
