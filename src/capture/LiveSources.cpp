#include "capture/LiveSources.h"

#include "core/Clock.h"
#include "core/Log.h"
#include "core/Thread.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace lectern::capture {

// ===========================================================================
// LiveVideoSource

Result<std::shared_ptr<LiveVideoSource>> LiveVideoSource::start(std::unique_ptr<IVideoSource> source) {
    if (!source) return fail(ErrorCode::InvalidArgument, "null video source");
    std::shared_ptr<LiveVideoSource> live(new LiveVideoSource(std::move(source)));
    if (auto st = live->source_->start(*live); !st) return fail(std::move(st).error());
    live->running_.store(true, std::memory_order_release);
    const auto info = live->source_->info();
    LEC_INFO("capture", "video source '{}' ({}) started", info.name, toString(info.kind));
    return live;
}

LiveVideoSource::~LiveVideoSource() { stop(); }

void LiveVideoSource::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;
    source_->stop();
    std::lock_guard lock(latestMutex_);
    latest_ = {};
}

VideoSourceInfo LiveVideoSource::info() const { return source_->info(); }

std::optional<CapturedVideoFrame> LiveVideoSource::latestFrame() const {
    std::lock_guard lock(latestMutex_);
    if (!latest_.frame) return std::nullopt;
    CapturedVideoFrame copy = latest_.ref();
    if (!copy.frame) return std::nullopt;
    return copy;
}

void LiveVideoSource::setConsumer(IVideoFrameConsumer* consumer) {
    std::lock_guard lock(consumerMutex_);
    consumer_ = consumer;
}

void LiveVideoSource::onVideoFrame(CapturedVideoFrame&& frame) noexcept {
    framesReceived_.fetch_add(1, std::memory_order_relaxed);
    lastFrameHostNs_.store(frame.hostTimeNs, std::memory_order_relaxed);
    if (frame.frame) {
        width_.store(frame.frame->width, std::memory_order_relaxed);
        height_.store(frame.frame->height, std::memory_order_relaxed);
    }
    {
        std::lock_guard lock(consumerMutex_);
        if (consumer_) consumer_->consumeVideoFrame(frame);
    }
    CapturedVideoFrame previous;
    {
        std::lock_guard lock(latestMutex_);
        previous = std::move(latest_);
        latest_ = std::move(frame);
    }
    // `previous` is released here, outside the lock.
}

void LiveVideoSource::onSourceEvent(const SourceEvent& event) noexcept {
    if (event.type == SourceEvent::Type::VideoEffectStarted) videoEffect_.store(true, std::memory_order_relaxed);
    if (event.type == SourceEvent::Type::VideoEffectStopped || event.type == SourceEvent::Type::Stopped) {
        videoEffect_.store(false, std::memory_order_relaxed);
    }
    {
        std::lock_guard lock(eventMutex_);
        lastEvent_ = event;
    }
    std::lock_guard lock(consumerMutex_);
    if (consumer_) consumer_->videoSourceEvent(event);
}

LiveVideoStats LiveVideoSource::stats() const {
    LiveVideoStats s;
    s.framesReceived = framesReceived_.load(std::memory_order_relaxed);
    s.droppedBySource = source_->droppedBySource();
    s.lastFrameHostNs = lastFrameHostNs_.load(std::memory_order_relaxed);
    s.width = width_.load(std::memory_order_relaxed);
    s.height = height_.load(std::memory_order_relaxed);
    return s;
}

std::optional<SourceEvent> LiveVideoSource::lastEvent() const {
    std::lock_guard lock(eventMutex_);
    return lastEvent_;
}

// ===========================================================================
// LiveAudioSource

LiveAudioSource::LiveAudioSource(std::unique_ptr<IAudioSource> source, const Options& options,
                                 std::size_t ringSamples)
    : source_(std::move(source)), options_(options), ring_(ringSamples), markers_(options.markerCapacity) {}

Result<std::shared_ptr<LiveAudioSource>> LiveAudioSource::start(std::unique_ptr<IAudioSource> source) {
    return start(std::move(source), Options{});
}

Result<std::shared_ptr<LiveAudioSource>> LiveAudioSource::start(std::unique_ptr<IAudioSource> source,
                                                                const Options& options) {
    if (!source) return fail(ErrorCode::InvalidArgument, "null audio source");
    // Size the ring from the real format: ringSeconds at the source rate and
    // channel count (bounded so a misreported format cannot explode memory).
    const AudioSourceInfo info = source->info();
    const int rate = std::clamp(info.sampleRate > 0 ? info.sampleRate : 48'000, 8'000, options.maxSampleRate);
    const int channels = std::clamp(info.channels, 1, options.maxChannels);
    const auto ringSamples = static_cast<std::size_t>(options.ringSeconds * rate * channels);
    std::shared_ptr<LiveAudioSource> live(new LiveAudioSource(std::move(source), options, ringSamples));
    live->meter_.setSampleRate(rate);
    live->running_.store(true, std::memory_order_release);
    live->pump_ = std::jthread([raw = live.get()](std::stop_token st) { raw->pumpLoop(st); });
    if (auto st = live->source_->start(*live); !st) {
        live->running_.store(false, std::memory_order_release);
        live->pump_.request_stop();
        live->dataReady_.release();
        live->pump_.join();
        return fail(std::move(st).error());
    }
    LEC_INFO("capture", "audio source '{}' ({}) started: {} Hz x{}, latency compensation {:.1f} ms", info.name,
             toString(info.kind), info.sampleRate, info.channels, static_cast<double>(info.latencyCompensationNs) / 1e6);
    return live;
}

LiveAudioSource::~LiveAudioSource() { stop(); }

void LiveAudioSource::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;
    source_->stop();
    if (pump_.joinable()) {
        pump_.request_stop();
        dataReady_.release();
        pump_.join();
    }
}

AudioSourceInfo LiveAudioSource::info() const { return source_->info(); }

void LiveAudioSource::setConsumer(IAudioBlockConsumer* consumer) {
    std::lock_guard lock(consumerMutex_);
    consumer_ = consumer;
}

void LiveAudioSource::onAudio(const AudioChunk& chunk) noexcept {
    // Real-time context: no locks, no allocation, no logging.
    if (chunk.frames <= 0 || chunk.channels <= 0 || chunk.channels > options_.maxChannels || !chunk.interleaved) return;
    const std::size_t samples = static_cast<std::size_t>(chunk.frames) * static_cast<std::size_t>(chunk.channels);
    framesReceived_.fetch_add(static_cast<std::uint64_t>(chunk.frames), std::memory_order_relaxed);

    if (options_.blockWhenFull) {
        // Deterministic tests: wait for the pump instead of dropping.
        while ((ring_.writeAvailable() < samples || markers_.writeAvailable() == 0) &&
               running_.load(std::memory_order_acquire)) {
            dataReady_.release();
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }
    if (ring_.writeAvailable() < samples || markers_.writeAvailable() == 0) {
        overflowFrames_.fetch_add(static_cast<std::uint64_t>(chunk.frames), std::memory_order_relaxed);
        pendingDiscontinuity_ = true;
        return;
    }
    ring_.write(chunk.interleaved, samples);
    std::uint32_t flags = chunk.flags;
    if (pendingDiscontinuity_) {
        flags |= AudioChunk::kDiscontinuity;
        pendingDiscontinuity_ = false;
    }
    markers_.push(Marker{chunk.hostTimeNs, chunk.frames, chunk.channels, chunk.sampleRate, flags});
    lastChunkHostNs_.store(chunk.hostTimeNs, std::memory_order_relaxed);
    sampleRate_.store(chunk.sampleRate, std::memory_order_relaxed);
    channels_.store(chunk.channels, std::memory_order_relaxed);
    dataReady_.release();
}

void LiveAudioSource::onSourceEvent(const SourceEvent& event) noexcept {
    {
        std::lock_guard lock(eventMutex_);
        lastEvent_ = event;
    }
    std::lock_guard lock(consumerMutex_);
    if (consumer_) consumer_->audioSourceEvent(event);
}

void LiveAudioSource::pumpAvailable() {
    Marker m{};
    while (markers_.pop(m)) {
        const std::size_t samples = static_cast<std::size_t>(m.frames) * static_cast<std::size_t>(m.channels);
        if (scratch_.size() < samples) scratch_.resize(samples);
        const std::size_t got = ring_.read(scratch_.data(), samples);
        if (got != samples) continue;  // cannot happen: samples are written before the marker
        meter_.process(scratch_.data(), m.frames, m.channels, HostClock::now());
        AudioBlock block{scratch_.data(), m.frames, m.channels, m.sampleRate, m.hostTimeNs, sampleIndex_,
                         (m.flags & AudioChunk::kDiscontinuity) != 0};
        sampleIndex_ += m.frames;
        {
            std::lock_guard lock(consumerMutex_);
            if (consumer_) consumer_->consumeAudio(block);
        }
        framesPumped_.fetch_add(static_cast<std::uint64_t>(m.frames), std::memory_order_release);
    }
}

void LiveAudioSource::pumpLoop(std::stop_token stop) {
    setCurrentThreadName("lectern.apump");
    setCurrentThreadPriority(ThreadPriority::AudioRealtime);
    scratch_.reserve(16384);
    while (!stop.stop_requested()) {
        (void)dataReady_.try_acquire_for(std::chrono::milliseconds(20));
        while (dataReady_.try_acquire()) {
        }
        pumpAvailable();
    }
    pumpAvailable();
}

void LiveAudioSource::waitUntilDrained(std::chrono::milliseconds timeout) const {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (framesPumped_.load(std::memory_order_acquire) + overflowFrames_.load(std::memory_order_relaxed) <
               framesReceived_.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

LiveAudioStats LiveAudioSource::stats() const {
    LiveAudioStats s;
    s.framesReceived = framesReceived_.load(std::memory_order_relaxed);
    s.overflowFrames = overflowFrames_.load(std::memory_order_relaxed);
    s.lastChunkHostNs = lastChunkHostNs_.load(std::memory_order_relaxed);
    s.sampleRate = sampleRate_.load(std::memory_order_relaxed);
    s.channels = channels_.load(std::memory_order_relaxed);
    return s;
}

std::optional<SourceEvent> LiveAudioSource::lastEvent() const {
    std::lock_guard lock(eventMutex_);
    return lastEvent_;
}

}  // namespace lectern::capture
