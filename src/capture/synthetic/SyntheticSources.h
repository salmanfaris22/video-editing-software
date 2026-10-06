#pragma once

// Deterministic test sources. They drive the full recording pipeline without
// devices: in CI, on platforms whose backends are not implemented yet, and in
// sync/drift/crash tests (docs/RECORDING_ENGINE.md §13).

#include "capture/CaptureInterfaces.h"
#include "core/Clock.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace lectern::capture {

using HostInterval = std::pair<std::int64_t, std::int64_t>;  ///< [begin, end) host ns

struct SyntheticVideoOptions {
    std::string name = "Synthetic Display";
    std::string deviceId = "synthetic-display";
    SourceKind kind = SourceKind::Display;
    int width = 640;
    int height = 360;
    FrameRate frameRate{30, 1};
    double jitterMs = 0.0;               ///< uniform timestamp jitter ±jitterMs
    std::int64_t flashPeriodNs = 0;      ///< > 0: frames are white during [k·P, k·P + flashDuration)
    std::int64_t flashDurationNs = 0;
    std::vector<HostInterval> stalls;    ///< no frames are produced inside these intervals
    bool realtime = true;                ///< own thread, paced by the steady clock
    const IClock* clock = nullptr;       ///< timestamps (null → HostClock)
    bool hardwareFrames = false;         ///< reserved for platform tests
};

class SyntheticVideoSource final : public IVideoSource {
public:
    explicit SyntheticVideoSource(SyntheticVideoOptions options);
    ~SyntheticVideoSource() override;

    Status start(IVideoFrameSink& sink) override;
    void stop() override;
    [[nodiscard]] VideoSourceInfo info() const override;

    /// Manual mode: produce one frame captured at `hostNs` (delivered synchronously).
    void produceFrame(std::int64_t hostNs);
    /// Manual mode: produce every frame of the nominal cadence with capture
    /// time in (last produced, untilNs], starting at `originNs` on first use.
    void produceUntil(std::int64_t untilNs, std::int64_t originNs = 0);

    [[nodiscard]] std::uint64_t framesProduced() const noexcept { return produced_.load(); }

private:
    void run();
    bool stalled(std::int64_t hostNs) const;

    SyntheticVideoOptions options_;
    const IClock& clock_;
    IVideoFrameSink* sink_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> produced_{0};
    std::int64_t nextIndex_ = 0;
    std::int64_t origin_ = 0;
    bool originSet_ = false;
    std::uint32_t rng_ = 12345;
};

struct SyntheticAudioOptions {
    std::string name = "Synthetic Microphone";
    std::string deviceId = "synthetic-microphone";
    SourceKind kind = SourceKind::Microphone;
    int sampleRate = 48'000;
    int channels = 1;
    int chunkFrames = 480;
    double toneHz = 440.0;
    float toneAmplitude = 0.1f;
    double driftPpm = 0.0;               ///< device clock error: +100 → 100 ppm fast
    double jitterMs = 0.0;               ///< timestamp jitter ±jitterMs
    std::int64_t clickPeriodNs = 0;      ///< > 0: loud burst at every k·P host time
    std::int64_t clickDurationNs = 2'000'000;
    float clickAmplitude = 0.9f;
    std::vector<HostInterval> gaps;      ///< chunks inside these intervals are lost
    std::int64_t latencyCompensationNs = 0;
    bool realtime = true;
    const IClock* clock = nullptr;
};

class SyntheticAudioSource final : public IAudioSource {
public:
    explicit SyntheticAudioSource(SyntheticAudioOptions options);
    ~SyntheticAudioSource() override;

    Status start(IAudioSink& sink) override;
    void stop() override;
    [[nodiscard]] AudioSourceInfo info() const override;

    /// Manual mode: delivers every chunk whose last sample is captured at or
    /// before `untilNs`. The device timeline starts at `originNs` on first use.
    void produceUntil(std::int64_t untilNs, std::int64_t originNs = 0);
    /// Host capture time of device sample `n` (exact device model).
    [[nodiscard]] double hostTimeOfSample(std::int64_t n) const;

private:
    void run();
    void deliverChunk();

    SyntheticAudioOptions options_;
    const IClock& clock_;
    IAudioSink* sink_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::int64_t nextSample_ = 0;
    std::int64_t origin_ = 0;
    bool originSet_ = false;
    std::vector<float> buffer_;
    std::uint32_t rng_ = 54321;
};

/// Backends that expose synthetic devices: one display, one camera, one
/// microphone and system audio. Permissions are always granted.
CaptureBackends makeSyntheticBackends(const IClock* clock = nullptr);

}  // namespace lectern::capture
