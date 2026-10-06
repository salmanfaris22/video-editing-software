#include "capture/synthetic/SyntheticSources.h"

#include "capture/EncodingPresets.h"
#include "core/Thread.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <numbers>

namespace lectern::capture {

namespace {

// xorshift32: tiny deterministic PRNG for jitter.
double nextUniform(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (state & 0xFFFFFF) / static_cast<double>(0xFFFFFF) * 2.0 - 1.0;  // [-1, 1]
}

bool inIntervals(const std::vector<HostInterval>& intervals, std::int64_t t) {
    for (const auto& [b, e] : intervals) {
        if (t >= b && t < e) return true;
    }
    return false;
}

}  // namespace

// ===========================================================================
// Video

SyntheticVideoSource::SyntheticVideoSource(SyntheticVideoOptions options)
    : options_(std::move(options)), clock_(options_.clock ? *options_.clock : HostClock::shared()) {}

SyntheticVideoSource::~SyntheticVideoSource() { stop(); }

VideoSourceInfo SyntheticVideoSource::info() const {
    return VideoSourceInfo{options_.name, options_.kind, options_.deviceId, options_.width, options_.height,
                           options_.frameRate};
}

Status SyntheticVideoSource::start(IVideoFrameSink& sink) {
    sink_ = &sink;
    running_.store(true);
    sink.onSourceEvent({SourceEvent::Type::Started, options_.name});
    if (options_.realtime) thread_ = std::thread([this] { run(); });
    return ok();
}

void SyntheticVideoSource::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    if (sink_) sink_->onSourceEvent({SourceEvent::Type::Stopped, options_.name});
}

bool SyntheticVideoSource::stalled(std::int64_t hostNs) const { return inIntervals(options_.stalls, hostNs); }

void SyntheticVideoSource::produceFrame(std::int64_t hostNs) {
    if (!running_.load() || !sink_ || stalled(hostNs)) return;
    auto frame = media::Frame::allocVideo(options_.width, options_.height, AV_PIX_FMT_NV12);
    if (!frame) return;
    AVFrame* f = frame->get();
    const bool flash = options_.flashPeriodNs > 0 && options_.flashDurationNs > 0 &&
                       (hostNs % options_.flashPeriodNs + options_.flashPeriodNs) % options_.flashPeriodNs <
                           options_.flashDurationNs;
    const std::uint64_t index = produced_.load();
    if (flash) {
        for (int y = 0; y < f->height; ++y) std::memset(f->data[0] + y * f->linesize[0], 235, static_cast<std::size_t>(f->width));
    } else {
        // Dark gradient with a moving bright bar (makes motion visible and
        // keeps the encoder honest about real changes).
        const int barX = static_cast<int>((index * 8) % static_cast<std::uint64_t>(std::max(1, f->width - 16)));
        for (int y = 0; y < f->height; ++y) {
            std::uint8_t* row = f->data[0] + y * f->linesize[0];
            for (int x = 0; x < f->width; ++x) row[x] = static_cast<std::uint8_t>(32 + (x * 64) / std::max(1, f->width));
            std::memset(row + barX, 160, 16);
        }
    }
    for (int y = 0; y < f->height / 2; ++y) std::memset(f->data[1] + y * f->linesize[1], 128, static_cast<std::size_t>(f->width));
    f->color_range = AVCOL_RANGE_MPEG;
    f->colorspace = AVCOL_SPC_BT709;
    f->color_primaries = AVCOL_PRI_BT709;
    f->color_trc = AVCOL_TRC_BT709;

    std::int64_t ts = hostNs;
    if (options_.jitterMs > 0) ts += static_cast<std::int64_t>(nextUniform(rng_) * options_.jitterMs * 1e6);
    produced_.fetch_add(1);
    sink_->onVideoFrame(CapturedVideoFrame{std::move(*frame), ts, index});
}

void SyntheticVideoSource::produceUntil(std::int64_t untilNs, std::int64_t originNs) {
    if (!originSet_) {
        origin_ = originNs;
        originSet_ = true;
        nextIndex_ = 0;
    }
    const Time period = options_.frameRate.frameDuration();
    for (;;) {
        const std::int64_t t = origin_ + Time::fromTicks(period.ticks() * nextIndex_).toNanoseconds();
        if (t > untilNs) break;
        produceFrame(t);
        ++nextIndex_;
    }
}

void SyntheticVideoSource::run() {
    setCurrentThreadName("lectern.synthetic.video");
    const auto period = std::chrono::nanoseconds(options_.frameRate.frameDuration().toNanoseconds());
    auto next = std::chrono::steady_clock::now();
    while (running_.load()) {
        produceFrame(clock_.nowNs());
        next += period;
        std::this_thread::sleep_until(next);
    }
}

// ===========================================================================
// Audio

SyntheticAudioSource::SyntheticAudioSource(SyntheticAudioOptions options)
    : options_(std::move(options)), clock_(options_.clock ? *options_.clock : HostClock::shared()) {
    buffer_.resize(static_cast<std::size_t>(options_.chunkFrames * options_.channels));
}

SyntheticAudioSource::~SyntheticAudioSource() { stop(); }

AudioSourceInfo SyntheticAudioSource::info() const {
    return AudioSourceInfo{options_.name, options_.kind, options_.deviceId, options_.sampleRate, options_.channels,
                           options_.latencyCompensationNs};
}

Status SyntheticAudioSource::start(IAudioSink& sink) {
    sink_ = &sink;
    running_.store(true);
    sink.onSourceEvent({SourceEvent::Type::Started, options_.name});
    if (options_.realtime) thread_ = std::thread([this] { run(); });
    return ok();
}

void SyntheticAudioSource::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    if (sink_) sink_->onSourceEvent({SourceEvent::Type::Stopped, options_.name});
}

double SyntheticAudioSource::hostTimeOfSample(std::int64_t n) const {
    // The device clock runs (1 + drift) times faster than nominal: sample n is
    // captured at origin + n / (rate · (1 + drift)).
    const double actualRate = options_.sampleRate * (1.0 + options_.driftPpm * 1e-6);
    return static_cast<double>(origin_) + static_cast<double>(n) * 1e9 / actualRate;
}

void SyntheticAudioSource::deliverChunk() {
    const int frames = options_.chunkFrames;
    const int ch = options_.channels;
    const std::int64_t first = nextSample_;
    const double t0 = hostTimeOfSample(first);
    nextSample_ += frames;
    if (inIntervals(options_.gaps, static_cast<std::int64_t>(t0))) return;  // lost chunk

    const double w = 2.0 * std::numbers::pi * options_.toneHz / options_.sampleRate;
    for (int i = 0; i < frames; ++i) {
        const std::int64_t n = first + i;
        float v = options_.toneAmplitude * static_cast<float>(std::sin(w * static_cast<double>(n)));
        if (options_.clickPeriodNs > 0) {
            const auto t = static_cast<std::int64_t>(hostTimeOfSample(n));
            const std::int64_t phase = ((t % options_.clickPeriodNs) + options_.clickPeriodNs) % options_.clickPeriodNs;
            if (phase < options_.clickDurationNs) v = (i & 1) ? options_.clickAmplitude : -options_.clickAmplitude;
        }
        for (int c = 0; c < ch; ++c) buffer_[static_cast<std::size_t>(i * ch + c)] = v;
    }
    std::int64_t ts = static_cast<std::int64_t>(std::llround(t0)) - 0;
    if (options_.jitterMs > 0) ts += static_cast<std::int64_t>(nextUniform(rng_) * options_.jitterMs * 1e6);
    sink_->onAudio(AudioChunk{buffer_.data(), frames, ch, options_.sampleRate, ts, 0});
}

void SyntheticAudioSource::produceUntil(std::int64_t untilNs, std::int64_t originNs) {
    if (!running_.load() || !sink_) return;
    if (!originSet_) {
        origin_ = originNs;
        originSet_ = true;
    }
    while (hostTimeOfSample(nextSample_ + options_.chunkFrames - 1) <= static_cast<double>(untilNs)) deliverChunk();
}

void SyntheticAudioSource::run() {
    setCurrentThreadName("lectern.synthetic.audio");
    origin_ = clock_.nowNs();
    originSet_ = true;
    while (running_.load()) {
        const double nextEnd = hostTimeOfSample(nextSample_ + options_.chunkFrames);
        const auto wait = static_cast<std::int64_t>(nextEnd) - clock_.nowNs();
        if (wait > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(wait));
        deliverChunk();
    }
}

// ===========================================================================
// Backends

namespace {

class SyntheticScreenBackend final : public IScreenCaptureBackend {
public:
    explicit SyntheticScreenBackend(const IClock* clock) : clock_(clock) {}
    [[nodiscard]] std::string name() const override { return "synthetic"; }
    Result<ScreenTargets> enumerateTargets() override {
        ScreenTargets t;
        t.displays.push_back({"synthetic-display", "Synthetic Display", 1920, 1080, 60.0, true});
        t.windows.push_back({"synthetic-window", "Synthetic Window", "Synthetic App", "synthetic.app", 1280, 720, true});
        t.applications.push_back({"synthetic.app", "Synthetic App", 1});
        return t;
    }
    Result<std::unique_ptr<IVideoSource>> createSource(const ScreenCaptureConfig& config) override {
        SyntheticVideoOptions o;
        const Resolution r = fitWithin({1920, 1080}, {config.maxWidth, config.maxHeight});
        o.width = r.width;
        o.height = r.height;
        o.frameRate = config.frameRate;
        o.clock = clock_;
        o.name = config.target.kind == ScreenTargetKind::Display ? "Synthetic Display" : "Synthetic Window";
        o.deviceId = config.target.id;
        o.kind = config.target.kind == ScreenTargetKind::Display ? SourceKind::Display : SourceKind::Window;
        return std::unique_ptr<IVideoSource>(new SyntheticVideoSource(o));
    }

private:
    const IClock* clock_;
};

class SyntheticCameraBackend final : public ICameraCaptureBackend {
public:
    explicit SyntheticCameraBackend(const IClock* clock) : clock_(clock) {}
    [[nodiscard]] std::string name() const override { return "synthetic"; }
    Result<std::vector<CameraInfo>> enumerateCameras() override {
        return std::vector<CameraInfo>{{"synthetic-camera", "Synthetic Camera", "Synthetic", true, false,
                                        {{1280, 720, 1, 30}, {1920, 1080, 1, 30}}}};
    }
    Result<std::unique_ptr<IVideoSource>> createSource(const CameraCaptureConfig& config) override {
        SyntheticVideoOptions o;
        const Resolution r = fitWithin({1280, 720}, {config.maxWidth, config.maxHeight});
        o.width = r.width;
        o.height = r.height;
        o.frameRate = config.frameRate;
        o.clock = clock_;
        o.name = "Synthetic Camera";
        o.deviceId = "synthetic-camera";
        o.kind = SourceKind::Camera;
        return std::unique_ptr<IVideoSource>(new SyntheticVideoSource(o));
    }

private:
    const IClock* clock_;
};

class SyntheticAudioBackend final : public IAudioCaptureBackend {
public:
    explicit SyntheticAudioBackend(const IClock* clock) : clock_(clock) {}
    [[nodiscard]] std::string name() const override { return "synthetic"; }
    Result<std::vector<AudioDeviceInfo>> enumerateInputs() override {
        return std::vector<AudioDeviceInfo>{{"synthetic-microphone", "Synthetic Microphone", 1, 48'000, true, "virtual"}};
    }
    Result<std::unique_ptr<IAudioSource>> createMicrophoneSource(const AudioCaptureConfig&) override {
        SyntheticAudioOptions o;
        o.clock = clock_;
        return std::unique_ptr<IAudioSource>(new SyntheticAudioSource(o));
    }
    [[nodiscard]] bool supportsSystemAudio() const override { return true; }
    Result<std::unique_ptr<IAudioSource>> createSystemAudioSource(const AudioCaptureConfig&) override {
        SyntheticAudioOptions o;
        o.clock = clock_;
        o.name = "Synthetic System Audio";
        o.deviceId = "synthetic-system-audio";
        o.kind = SourceKind::SystemAudio;
        o.channels = 2;
        o.toneHz = 660.0;
        o.toneAmplitude = 0.05f;
        return std::unique_ptr<IAudioSource>(new SyntheticAudioSource(o));
    }

private:
    const IClock* clock_;
};

class AlwaysGrantedPermissions final : public IPermissionService {
public:
    [[nodiscard]] PermissionStatus status(PermissionKind) const override { return PermissionStatus::Granted; }
    void request(PermissionKind, std::function<void(PermissionStatus)> done) override {
        if (done) done(PermissionStatus::Granted);
    }
};

}  // namespace

CaptureBackends makeSyntheticBackends(const IClock* clock) {
    CaptureBackends b;
    b.screen = std::make_unique<SyntheticScreenBackend>(clock);
    b.camera = std::make_unique<SyntheticCameraBackend>(clock);
    b.audio = std::make_unique<SyntheticAudioBackend>(clock);
    b.permissions = std::make_unique<AlwaysGrantedPermissions>();
    return b;
}

}  // namespace lectern::capture
