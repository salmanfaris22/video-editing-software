#pragma once

#include "audio/LevelMeter.h"
#include "capture/CaptureInterfaces.h"
#include "core/SpscRing.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <semaphore>
#include <thread>
#include <vector>

namespace lectern::capture {

// ---------------------------------------------------------------------------
// Video

/// Receives frames from a LiveVideoSource on the capture thread. Must not
/// block; keep a frame by taking a reference (`frame.ref()`).
class IVideoFrameConsumer {
public:
    virtual ~IVideoFrameConsumer() = default;
    virtual void consumeVideoFrame(const CapturedVideoFrame& frame) noexcept = 0;
    virtual void videoSourceEvent(const SourceEvent& /*event*/) noexcept {}
};

struct LiveVideoStats {
    std::uint64_t framesReceived = 0;
    std::uint64_t droppedBySource = 0;
    std::int64_t lastFrameHostNs = 0;
    int width = 0;
    int height = 0;
};

/// A running video source plus its fan-out: the latest-frame slot (previews)
/// and an optional recording consumer. Outlives individual recordings, so
/// devices are warm when the user presses record
/// (docs/RECORDING_ENGINE.md §2).
class LiveVideoSource final : public IVideoFrameSink {
public:
    static Result<std::shared_ptr<LiveVideoSource>> start(std::unique_ptr<IVideoSource> source);
    ~LiveVideoSource() override;
    LiveVideoSource(const LiveVideoSource&) = delete;
    LiveVideoSource& operator=(const LiveVideoSource&) = delete;

    void stop();
    [[nodiscard]] bool isRunning() const noexcept { return running_.load(std::memory_order_acquire); }
    [[nodiscard]] VideoSourceInfo info() const;

    /// Newest frame (shared reference) for previews and pacer seeding.
    [[nodiscard]] std::optional<CapturedVideoFrame> latestFrame() const;

    /// Attaches/detaches (nullptr) the recording consumer. Returns only after
    /// any in-flight delivery to the previous consumer has finished.
    void setConsumer(IVideoFrameConsumer* consumer);

    [[nodiscard]] LiveVideoStats stats() const;
    [[nodiscard]] std::optional<SourceEvent> lastEvent() const;
    /// The OS is compositing a video effect into this source right now
    /// (e.g. Presenter Overlay puts the camera into the screen recording).
    [[nodiscard]] bool videoEffectActive() const noexcept { return videoEffect_.load(std::memory_order_relaxed); }

private:
    explicit LiveVideoSource(std::unique_ptr<IVideoSource> source) : source_(std::move(source)) {}
    void onVideoFrame(CapturedVideoFrame&& frame) noexcept override;
    void onSourceEvent(const SourceEvent& event) noexcept override;

    std::unique_ptr<IVideoSource> source_;
    std::atomic<bool> running_{false};

    mutable std::mutex latestMutex_;
    CapturedVideoFrame latest_;

    std::mutex consumerMutex_;
    IVideoFrameConsumer* consumer_ = nullptr;

    mutable std::mutex eventMutex_;
    std::optional<SourceEvent> lastEvent_;

    std::atomic<std::uint64_t> framesReceived_{0};
    std::atomic<std::int64_t> lastFrameHostNs_{0};
    std::atomic<int> width_{0};
    std::atomic<int> height_{0};
    std::atomic<bool> videoEffect_{false};
};

// ---------------------------------------------------------------------------
// Audio

/// A block of audio delivered on the source's pump thread (not real-time).
struct AudioBlock {
    const float* interleaved = nullptr;
    int frames = 0;
    int channels = 0;
    int sampleRate = 0;
    std::int64_t hostTimeNs = 0;   ///< capture time of the first frame (unsmoothed)
    std::int64_t sampleIndex = 0;  ///< running count of frames delivered by this source
    bool discontinuity = false;    ///< frames were lost before this block
};

class IAudioBlockConsumer {
public:
    virtual ~IAudioBlockConsumer() = default;
    virtual void consumeAudio(const AudioBlock& block) noexcept = 0;
    virtual void audioSourceEvent(const SourceEvent& /*event*/) noexcept {}
};

struct LiveAudioStats {
    std::uint64_t framesReceived = 0;
    std::uint64_t overflowFrames = 0;  ///< dropped in the RT callback (ring full)
    std::int64_t lastChunkHostNs = 0;
    int sampleRate = 0;
    int channels = 0;
};

/// A running audio source: real-time callback → lock-free ring → pump
/// thread → level meter + optional recording consumer.
class LiveAudioSource final : public IAudioSink {
public:
    struct Options {
        double ringSeconds = 2.0;
        std::size_t markerCapacity = 1024;
        int maxChannels = 8;
        int maxSampleRate = 192'000;
        /// Tests only: the producer waits for ring space instead of dropping.
        bool blockWhenFull = false;
    };

    static Result<std::shared_ptr<LiveAudioSource>> start(std::unique_ptr<IAudioSource> source);
    static Result<std::shared_ptr<LiveAudioSource>> start(std::unique_ptr<IAudioSource> source, const Options& options);
    ~LiveAudioSource() override;
    LiveAudioSource(const LiveAudioSource&) = delete;
    LiveAudioSource& operator=(const LiveAudioSource&) = delete;

    void stop();
    [[nodiscard]] bool isRunning() const noexcept { return running_.load(std::memory_order_acquire); }
    [[nodiscard]] AudioSourceInfo info() const;
    [[nodiscard]] audio::LevelSnapshot levels() const noexcept { return meter_.snapshot(); }
    /// Attaches/detaches the consumer; waits for in-flight delivery to finish.
    void setConsumer(IAudioBlockConsumer* consumer);
    [[nodiscard]] LiveAudioStats stats() const;
    [[nodiscard]] std::optional<SourceEvent> lastEvent() const;
    /// Blocks until all audio delivered so far has been pumped to the consumer.
    /// Used by deterministic tests and by the session when stopping.
    void waitUntilDrained(std::chrono::milliseconds timeout) const;

private:
    struct Marker {
        std::int64_t hostTimeNs;
        std::int32_t frames;
        std::int32_t channels;
        std::int32_t sampleRate;
        std::uint32_t flags;
    };

    LiveAudioSource(std::unique_ptr<IAudioSource> source, const Options& options, std::size_t ringSamples);
    void onAudio(const AudioChunk& chunk) noexcept override;  // real-time
    void onSourceEvent(const SourceEvent& event) noexcept override;
    void pumpLoop(std::stop_token stop);
    void pumpAvailable();

    std::unique_ptr<IAudioSource> source_;
    Options options_;
    SpscRing<float> ring_;
    SpscRing<Marker> markers_;
    std::counting_semaphore<(1 << 30)> dataReady_{0};
    std::atomic<bool> running_{false};
    bool pendingDiscontinuity_ = false;  // RT thread only

    std::jthread pump_;
    std::vector<float> scratch_;  // pump thread only
    std::int64_t sampleIndex_ = 0;
    audio::LevelMeter meter_;

    std::mutex consumerMutex_;
    IAudioBlockConsumer* consumer_ = nullptr;

    mutable std::mutex eventMutex_;
    std::optional<SourceEvent> lastEvent_;

    std::atomic<std::uint64_t> framesReceived_{0};
    std::atomic<std::uint64_t> framesPumped_{0};
    std::atomic<std::uint64_t> overflowFrames_{0};
    std::atomic<std::int64_t> lastChunkHostNs_{0};
    std::atomic<int> sampleRate_{0};
    std::atomic<int> channels_{0};
};

}  // namespace lectern::capture
