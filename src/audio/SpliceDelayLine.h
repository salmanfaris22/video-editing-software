#pragma once

#include <cstddef>
#include <functional>
#include <vector>

namespace lectern::audio {

/// Short output delay line that makes clean splices possible.
///
/// Audio passes through with a fixed delay (default 10 ms). When the stream is
/// cut (pause, stop, hard correction), the samples just before the cut are
/// still in the buffer, so a fade-out can be applied *retroactively*; the
/// first samples after the cut get a fade-in. This removes clicks without a
/// look-ahead in the capture path. The delay does not shift timestamps: the
/// encoder counts samples in order.
class SpliceDelayLine {
public:
    using Emit = std::function<void(const float* interleaved, int frames)>;

    SpliceDelayLine(int channels, int delayFrames, int fadeFrames);

    /// Appends samples and emits everything older than the delay.
    void push(const float* interleaved, int frames, const Emit& emit);
    /// Appends `frames` of silence (gap filling).
    void pushSilence(int frames, const Emit& emit);
    /// Fades out the buffered tail (up to fadeFrames) — call at a cut.
    void fadeOutTail() noexcept;
    /// The next fadeFrames pushed samples are faded in.
    void armFadeIn() noexcept { fadeInPos_ = 0; }
    /// Drops up to `frames` from the end of the buffer; returns how many were dropped.
    int truncateTail(int frames) noexcept;
    /// Emits everything buffered.
    void flush(const Emit& emit);

    [[nodiscard]] int bufferedFrames() const noexcept {
        return static_cast<int>((buffer_.size() - readPos_) / static_cast<std::size_t>(channels_));
    }
    [[nodiscard]] int channels() const noexcept { return channels_; }

private:
    void emitOlderThanDelay(const Emit& emit);
    void compact();

    int channels_;
    int delayFrames_;
    int fadeFrames_;
    int fadeInPos_;  // == fadeFrames_ when no fade-in is active
    std::vector<float> buffer_;
    std::size_t readPos_ = 0;  // in floats
    std::vector<float> silence_;
};

}  // namespace lectern::audio
