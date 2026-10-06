#pragma once

#include "media/Frame.h"
#include "media/VideoEncoder.h"  // PacketSink

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lectern::media {

enum class AudioCodec { Flac, Aac, Pcm };

[[nodiscard]] std::string_view toString(AudioCodec codec) noexcept;

struct AudioEncoderConfig {
    AudioCodec codec = AudioCodec::Flac;
    int sampleRate = 48'000;
    int channels = 2;
    int bitsPerSample = 24;          ///< FLAC/PCM: 16 or 24
    std::int64_t bitRate = 192'000;  ///< AAC only
    std::vector<std::string> encoderNames;  ///< optional explicit candidates
};

/// Encodes interleaved float32 audio. Handles sample-format conversion and
/// re-chunking to the codec's frame size; output timestamps count samples
/// from `startSample` in a 1/sampleRate time base.
class AudioEncoder {
public:
    static Result<std::unique_ptr<AudioEncoder>> create(const AudioEncoderConfig& config);
    ~AudioEncoder();
    AudioEncoder(const AudioEncoder&) = delete;
    AudioEncoder& operator=(const AudioEncoder&) = delete;

    /// Sample index (in output samples) of the first sample submitted. Must be
    /// called before the first encode(); it encodes the track's start offset.
    void setStartSample(std::int64_t sample) noexcept { nextPts_ = sample; }

    Status encode(const float* interleaved, int frames, const PacketSink& sink);
    /// Encodes `frames` of digital silence.
    Status encodeSilence(int frames, const PacketSink& sink);
    Status flush(const PacketSink& sink);

    [[nodiscard]] const AVCodecContext& context() const noexcept { return *ctx_; }
    [[nodiscard]] AVRational timeBase() const noexcept { return ctx_->time_base; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    /// Samples submitted so far (including silence).
    [[nodiscard]] std::int64_t samplesSubmitted() const noexcept { return submitted_; }

private:
    AudioEncoder() = default;
    Status pushFifo(const float* interleaved, int frames);
    Status encodeAvailable(bool final, const PacketSink& sink);
    Status sendFrame(AVFrame* frame, const PacketSink& sink);

    AVCodecContextPtr ctx_;
    SwrContextPtr convert_;
    AVAudioFifoPtr fifo_;
    Frame frame_;
    std::vector<std::uint8_t*> planes_;
    std::vector<float> silence_;
    std::vector<std::uint8_t> scratch_;
    std::string name_;
    int frameSize_ = 1024;
    std::int64_t nextPts_ = 0;
    std::int64_t submitted_ = 0;
    bool flushed_ = false;
};

}  // namespace lectern::media
