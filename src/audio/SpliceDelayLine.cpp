#include "audio/SpliceDelayLine.h"

#include <algorithm>

namespace lectern::audio {

SpliceDelayLine::SpliceDelayLine(int channels, int delayFrames, int fadeFrames)
    : channels_(std::max(1, channels)),
      delayFrames_(std::max(0, delayFrames)),
      fadeFrames_(std::max(1, std::min(fadeFrames, std::max(1, delayFrames)))),
      fadeInPos_(fadeFrames_) {
    buffer_.reserve(static_cast<std::size_t>((delayFrames_ + 4096) * channels_));
}

void SpliceDelayLine::push(const float* interleaved, int frames, const Emit& emit) {
    if (frames <= 0) return;
    const std::size_t start = buffer_.size();
    buffer_.insert(buffer_.end(), interleaved, interleaved + static_cast<std::ptrdiff_t>(frames) * channels_);
    if (fadeInPos_ < fadeFrames_) {
        for (int i = 0; i < frames && fadeInPos_ < fadeFrames_; ++i, ++fadeInPos_) {
            const float g = static_cast<float>(fadeInPos_) / static_cast<float>(fadeFrames_);
            for (int c = 0; c < channels_; ++c) {
                buffer_[start + static_cast<std::size_t>(i * channels_ + c)] *= g;
            }
        }
    }
    emitOlderThanDelay(emit);
}

void SpliceDelayLine::pushSilence(int frames, const Emit& emit) {
    constexpr int kChunk = 4096;
    if (silence_.size() < static_cast<std::size_t>(kChunk * channels_)) {
        silence_.assign(static_cast<std::size_t>(kChunk * channels_), 0.0f);
    }
    while (frames > 0) {
        const int n = std::min(frames, kChunk);
        push(silence_.data(), n, emit);
        frames -= n;
    }
}

void SpliceDelayLine::fadeOutTail() noexcept {
    const int buffered = bufferedFrames();
    const int n = std::min(buffered, fadeFrames_);
    if (n <= 0) return;
    const std::size_t first = buffer_.size() - static_cast<std::size_t>(n * channels_);
    for (int i = 0; i < n; ++i) {
        // Gain goes from (n-1)/n down to 0 at the last sample before the cut.
        const float g = static_cast<float>(n - 1 - i) / static_cast<float>(n);
        for (int c = 0; c < channels_; ++c) buffer_[first + static_cast<std::size_t>(i * channels_ + c)] *= g;
    }
}

int SpliceDelayLine::truncateTail(int frames) noexcept {
    const int n = std::min(frames, bufferedFrames());
    if (n <= 0) return 0;
    buffer_.resize(buffer_.size() - static_cast<std::size_t>(n * channels_));
    return n;
}

void SpliceDelayLine::flush(const Emit& emit) {
    const int n = bufferedFrames();
    if (n > 0) emit(buffer_.data() + readPos_, n);
    buffer_.clear();
    readPos_ = 0;
}

void SpliceDelayLine::emitOlderThanDelay(const Emit& emit) {
    const int excess = bufferedFrames() - delayFrames_;
    if (excess <= 0) return;
    emit(buffer_.data() + readPos_, excess);
    readPos_ += static_cast<std::size_t>(excess * channels_);
    compact();
}

void SpliceDelayLine::compact() {
    // Shift the remaining samples to the front once the consumed prefix is
    // large, keeping memory bounded without per-push erases.
    if (readPos_ > static_cast<std::size_t>(8192 * channels_) || readPos_ * 2 > buffer_.size()) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(readPos_));
        readPos_ = 0;
    }
}

}  // namespace lectern::audio
