#pragma once

#include "audio/DriftController.h"
#include "audio/SpliceDelayLine.h"
#include "audio/TimestampSmoother.h"
#include "capture/LiveSources.h"
#include "capture/MuxWorker.h"
#include "capture/RecordingStats.h"
#include "capture/SessionClock.h"
#include "media/AudioEncoder.h"
#include "media/AudioResampler.h"
#include "media/VideoEncoder.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace lectern::capture {

/// Final description of a written track (goes into the manifest).
struct TrackOutcome {
    TrackState state = TrackState::Pending;
    std::optional<Error> error;
    std::optional<Time> start;  ///< session time of the first frame/sample in the file
    std::optional<Time> end;
    std::uint64_t bytes = 0;
    std::string codec;
    std::string encoder;
    bool hardwareEncoder = false;
    int width = 0;
    int height = 0;
    FrameRate frameRate;
    int sampleRate = 0;
    int channels = 0;
};

// ===========================================================================
// Video

struct VideoTrackSettings {
    std::string trackId;
    TrackRole role = TrackRole::Screen;
    std::filesystem::path outputPath;
    FrameRate frameRate{30, 1};
    media::VideoCodec codec = media::VideoCodec::H264;
    media::EncoderPreference encoder = media::EncoderPreference::Auto;
    std::int64_t bitRate = 10'000'000;
    int gopSeconds = 2;
    std::optional<Time> maxHold;  ///< nullopt: duplicate indefinitely (screens)
    Time latencyAllowance = Time::fromMilliseconds(100);
    Time drainTimeout = Time::fromMilliseconds(500);
    std::size_t inputQueueFrames = 6;
    bool blockWhenFull = false;  ///< offline/tests: back-pressure instead of drops
    MuxWorker::Options mux;
    media::MuxerOptions muxer = media::recordingMuxerOptions();
};

/// Encode thread for one video track: session-time mapping → CFR pacer →
/// encoder → MuxWorker (docs/RECORDING_ENGINE.md §5).
class VideoTrackWriter final : public IVideoFrameConsumer {
public:
    VideoTrackWriter(VideoTrackSettings settings, const SessionClock& clock);
    ~VideoTrackWriter() override;
    VideoTrackWriter(const VideoTrackWriter&) = delete;
    VideoTrackWriter& operator=(const VideoTrackWriter&) = delete;

    /// Creates the output file and starts the encode thread.
    Status open();
    /// Newest pre-roll frame, so slot 0 has content even on a static screen.
    void seed(CapturedVideoFrame frame);

    void consumeVideoFrame(const CapturedVideoFrame& frame) noexcept override;
    void videoSourceEvent(const SourceEvent& event) noexcept override;

    /// The next encoded frame is a keyframe (called on resume).
    void onResumed() noexcept { resumeKeyframe_.store(true, std::memory_order_release); }
    /// The session clock must already be stopped. The writer drains until it
    /// sees a frame captured after S or the drain timeout passes, emits the
    /// final slots up to S, flushes and finalizes.
    void requestStop() noexcept;
    /// Cancel: stop immediately, delete the file.
    void abort();
    bool waitFinished(std::chrono::milliseconds timeout);
    [[nodiscard]] bool isFinished() const noexcept { return finished_.load(std::memory_order_acquire); }

    [[nodiscard]] VideoTrackStats stats() const;
    [[nodiscard]] TrackOutcome outcome() const;
    [[nodiscard]] std::uint64_t bytesWritten() const noexcept { return mux_.bytesWritten(); }
    [[nodiscard]] std::size_t packetQueueBytes() const { return mux_.queuedBytes(); }
    [[nodiscard]] const VideoTrackSettings& settings() const noexcept { return settings_; }
    [[nodiscard]] bool hasFailed() const noexcept { return failed_.load(std::memory_order_acquire); }

private:
    void run();
    void encodeSlot(std::int64_t slot, media::Frame&& frame, bool duplicate, bool keyframe);
    bool openEncoder(const media::Frame& frame);
    void finalize();
    void setError(Error e);

    VideoTrackSettings settings_;
    const SessionClock& clock_;
    BoundedQueue<CapturedVideoFrame> input_;
    MuxWorker mux_;
    std::unique_ptr<media::VideoEncoder> encoder_;
    std::thread thread_;

    std::mutex seedMutex_;
    std::optional<CapturedVideoFrame> seed_;

    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> aborted_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> finished_{false};
    std::atomic<bool> resumeKeyframe_{false};
    std::atomic<bool> zeroCopy_{false};
    bool sawPostStopFrame_ = false;  // encode thread only
    bool firstEncoded_ = false;      // encode thread only

    // Stats (written by the encode/capture threads, read anywhere).
    std::atomic<std::uint64_t> framesReceived_{0};
    std::atomic<std::uint64_t> droppedQueueFull_{0};
    std::atomic<std::uint64_t> framesEncoded_{0};
    std::atomic<std::uint64_t> framesDuplicated_{0};
    std::atomic<std::uint64_t> framesDecimated_{0};
    std::atomic<std::uint64_t> framesLate_{0};
    std::atomic<std::uint64_t> gapSlots_{0};

    mutable std::mutex mutex_;
    std::condition_variable finishedCv_;
    TrackOutcome outcome_;
};

// ===========================================================================
// Audio

struct AudioTrackSettings {
    std::string trackId;
    TrackRole role = TrackRole::Microphone;
    std::filesystem::path outputPath;
    int outputSampleRate = 48'000;
    int maxChannels = 2;
    media::AudioCodec codec = media::AudioCodec::Flac;
    int bitsPerSample = 24;
    std::int64_t bitRate = 192'000;
    Time drainTimeout = Time::fromMilliseconds(300);
    Time fadeDuration = Time::fromMilliseconds(5);
    Time delayLine = Time::fromMilliseconds(10);
    double hardThresholdMs = 20.0;
    double maxCompensationRatio = 0.005;
    MuxWorker::Options mux{.queueBytes = 8ull << 20};
    media::MuxerOptions muxer = media::recordingMuxerOptions();
};

/// Writes one audio track. Runs on the live source's pump thread
/// (docs/RECORDING_ENGINE.md §6): timestamp smoothing → sample-accurate
/// split at T0/pauses/S → drift control → resampling → click-free splices →
/// FLAC → MuxWorker.
class AudioTrackWriter final : public IAudioBlockConsumer {
public:
    AudioTrackWriter(AudioTrackSettings settings, const SessionClock& clock, int sourceChannels);
    ~AudioTrackWriter() override;
    AudioTrackWriter(const AudioTrackWriter&) = delete;
    AudioTrackWriter& operator=(const AudioTrackWriter&) = delete;

    /// Creates the encoder and the file (the format is known up front).
    Status open();

    void consumeAudio(const AudioBlock& block) noexcept override;
    void audioSourceEvent(const SourceEvent& event) noexcept override;

    /// True once audio captured at or after the stop instant was processed.
    [[nodiscard]] bool reachedStop() const noexcept { return reachedStop_.load(std::memory_order_acquire); }
    /// Call after detaching from the source: pads/truncates to exactly S,
    /// flushes and finalizes the file (blocking).
    Status finish();
    void abort();

    [[nodiscard]] AudioTrackStats stats() const;
    [[nodiscard]] TrackOutcome outcome() const;
    [[nodiscard]] std::uint64_t bytesWritten() const noexcept { return mux_.bytesWritten(); }
    [[nodiscard]] std::size_t packetQueueBytes() const { return mux_.queuedBytes(); }
    [[nodiscard]] const AudioTrackSettings& settings() const noexcept { return settings_; }
    [[nodiscard]] bool hasFailed() const noexcept { return failed_.load(std::memory_order_acquire); }
    [[nodiscard]] int outputChannels() const noexcept { return outChannels_; }

private:
    void processBlock(const AudioBlock& block);
    void feed(const float* data, int frames, double targetPosition, bool splice);
    void emitToEncoder(const float* data, int frames);
    Status ensureResampler(int inputRate);
    void mapChannels(const AudioBlock& block);
    void setError(Error e);

    AudioTrackSettings settings_;
    const SessionClock& clock_;
    int outChannels_;
    MuxWorker mux_;
    std::unique_ptr<media::AudioEncoder> encoder_;
    std::unique_ptr<media::AudioResampler> resampler_;
    std::unique_ptr<audio::TimestampSmoother> smoother_;
    audio::DriftController drift_;
    audio::SpliceDelayLine delay_;
    audio::SpliceDelayLine::Emit emit_;

    int inputRate_ = 0;
    bool started_ = false;
    std::int64_t written_ = 0;  ///< output position (samples) of the next sample entering the delay line
    double lastFedHostEnd_ = 0;
    std::vector<float> mapped_;
    std::vector<float> resampled_;

    std::atomic<bool> reachedStop_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> finished_{false};
    std::atomic<bool> aborted_{false};

    std::atomic<std::uint64_t> framesIn_{0};
    std::atomic<std::uint64_t> framesOut_{0};
    std::atomic<std::uint64_t> silenceInserted_{0};
    std::atomic<std::uint64_t> inputDropped_{0};
    std::atomic<std::uint64_t> discontinuities_{0};
    std::atomic<double> driftPpm_{0};
    std::atomic<double> lastErrorMs_{0};
    std::atomic<std::int64_t> lastBlockHostNs_{0};

    mutable std::mutex mutex_;
    TrackOutcome outcome_;
};

}  // namespace lectern::capture
