#pragma once

#include "core/Time.h"
#include "media/Frame.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace lectern::media {

/// Sample-accurate audio by media time as interleaved stereo float at a
/// fixed rate. Mono sources play on both channels at full level (voice is
/// not dropped by 3 dB). Sequential reads decode on; jumps seek. Times
/// without audio (before the first sample, after the last, gaps) read as
/// silence. One thread per reader.
class AudioReader {
public:
    static constexpr int kChannels = 2;

    static Result<std::unique_ptr<AudioReader>> open(const std::filesystem::path& path, int sampleRate = 48'000);
    ~AudioReader();
    AudioReader(const AudioReader&) = delete;
    AudioReader& operator=(const AudioReader&) = delete;

    /// Writes `frames` stereo frames (2 × frames floats) for media time [t, t + frames/rate).
    Status read(Time t, int frames, float* out);

    [[nodiscard]] int sampleRate() const noexcept { return rate_; }
    [[nodiscard]] Time start() const noexcept { return start_; }
    [[nodiscard]] Time duration() const noexcept { return duration_; }
    [[nodiscard]] int sourceChannels() const noexcept { return sourceChannels_; }
    [[nodiscard]] std::uint64_t seeks() const noexcept { return seeks_; }

private:
    AudioReader() = default;
    Status initConverter();
    Status seek(std::int64_t sample);
    /// Decodes one more frame into the buffer; false at end of stream.
    Result<bool> decodeMore();
    void dropBefore(std::int64_t sample);

    AVFormatInputPtr format_;
    AVCodecContextPtr codec_;
    SwrContextPtr swr_;
    Frame frame_;
    int stream_ = -1;
    AVRational timeBase_{1, 1};
    int rate_ = 48'000;
    int sourceChannels_ = 0;
    Time start_;
    Time duration_;
    std::vector<float> buffer_;      ///< interleaved stereo, contiguous media samples
    std::int64_t bufferStart_ = 0;   ///< sample index (media time × rate) of buffer_[0]
    bool positioned_ = false;        ///< bufferStart_ is known (a frame was decoded since the seek)
    bool eof_ = false;
    bool drained_ = false;
    std::uint64_t seeks_ = 0;
};

}  // namespace lectern::media
