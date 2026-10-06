#include "capture/TrackWriters.h"
#include "capture/VideoPacer.h"

#include "core/Log.h"
#include "core/Thread.h"

#include <algorithm>

namespace lectern::capture {

VideoTrackWriter::VideoTrackWriter(VideoTrackSettings settings, const SessionClock& clock)
    : settings_(std::move(settings)),
      clock_(clock),
      input_(std::max<std::size_t>(1, settings_.inputQueueFrames)),
      mux_(settings_.trackId, settings_.outputPath, settings_.muxer, settings_.mux) {
    outcome_.frameRate = settings_.frameRate;
    outcome_.codec = std::string(media::toString(settings_.codec));
}

VideoTrackWriter::~VideoTrackWriter() {
    if (thread_.joinable()) {
        aborted_.store(true, std::memory_order_release);
        input_.close();
        thread_.join();
    }
}

Status VideoTrackWriter::open() {
    LEC_TRY(mux_.open());
    {
        std::lock_guard lock(mutex_);
        outcome_.state = TrackState::Recording;
    }
    thread_ = std::thread([this] { run(); });
    return ok();
}

void VideoTrackWriter::seed(CapturedVideoFrame frame) {
    std::lock_guard lock(seedMutex_);
    seed_ = std::move(frame);
}

void VideoTrackWriter::consumeVideoFrame(const CapturedVideoFrame& frame) noexcept {
    if (finished_.load(std::memory_order_acquire)) return;
    CapturedVideoFrame copy = frame.ref();
    if (!copy.frame) {
        droppedQueueFull_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    framesReceived_.fetch_add(1, std::memory_order_relaxed);
    const PushResult r = settings_.blockWhenFull ? input_.push(copy) : input_.tryPush(copy);
    if (r == PushResult::Full) droppedQueueFull_.fetch_add(1, std::memory_order_relaxed);
}

void VideoTrackWriter::videoSourceEvent(const SourceEvent& event) noexcept {
    if (event.type == SourceEvent::Type::Error || event.type == SourceEvent::Type::Interrupted) {
        LEC_WARN("capture", "track {}: source {}: {}", settings_.trackId, toString(event.type), event.message);
    }
}

void VideoTrackWriter::requestStop() noexcept { stopRequested_.store(true, std::memory_order_release); }

void VideoTrackWriter::abort() {
    aborted_.store(true, std::memory_order_release);
    input_.close();
    if (thread_.joinable()) thread_.join();
    mux_.abort(true);
    std::lock_guard lock(mutex_);
    outcome_.state = TrackState::Failed;
    if (!outcome_.error) outcome_.error = Error(ErrorCode::Cancelled, "cancelled");
    finished_.store(true, std::memory_order_release);
    finishedCv_.notify_all();
}

bool VideoTrackWriter::waitFinished(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return finishedCv_.wait_for(lock, timeout, [this] { return finished_.load(std::memory_order_acquire); });
}

void VideoTrackWriter::setError(Error e) {
    LEC_ERROR("capture", "track {}: {}", settings_.trackId, e.toString());
    std::lock_guard lock(mutex_);
    if (!outcome_.error) outcome_.error = std::move(e);
    failed_.store(true, std::memory_order_release);
}

bool VideoTrackWriter::openEncoder(const media::Frame& frame) {
    media::VideoEncoderConfig cfg;
    cfg.codec = settings_.codec;
    cfg.preference = settings_.encoder;
    cfg.width = frame->width & ~1;
    cfg.height = frame->height & ~1;
    cfg.frameRate = settings_.frameRate;
    cfg.bitRate = settings_.bitRate;
    cfg.gopFrames = std::max(1, static_cast<int>(settings_.frameRate.toDouble() * settings_.gopSeconds + 0.5));
    cfg.realtime = true;
    cfg.inputFormat = static_cast<AVPixelFormat>(frame->format);
    if (frame->hw_frames_ctx) {
        const auto* fc = reinterpret_cast<const AVHWFramesContext*>(frame->hw_frames_ctx->data);
        cfg.inputSwFormat = fc->sw_format;
        cfg.inputHwFramesCtx = frame->hw_frames_ctx;
    } else if (media::isHardwarePixelFormat(cfg.inputFormat)) {
        cfg.inputSwFormat = AV_PIX_FMT_NV12;
    }
    if (frame->color_range != AVCOL_RANGE_UNSPECIFIED) cfg.color.range = frame->color_range;
    if (frame->colorspace != AVCOL_SPC_UNSPECIFIED) cfg.color.space = frame->colorspace;
    if (frame->color_primaries != AVCOL_PRI_UNSPECIFIED) cfg.color.primaries = frame->color_primaries;
    if (frame->color_trc != AVCOL_TRC_UNSPECIFIED) cfg.color.transfer = frame->color_trc;

    auto enc = media::VideoEncoder::create(cfg);
    if (!enc) {
        setError(std::move(enc).error().withContext("track " + settings_.trackId));
        return false;
    }
    encoder_ = std::move(*enc);
    if (auto st = mux_.configure(encoder_->context()); !st) {
        setError(std::move(st).error());
        return false;
    }
    zeroCopy_.store(encoder_->identity().zeroCopy, std::memory_order_relaxed);
    LEC_INFO("capture", "track {}: {} ({}{}) {}x{} @ {} fps, {:.1f} Mbit/s", settings_.trackId, encoder_->identity().name,
             encoder_->identity().hardware ? "hardware" : "software", encoder_->identity().zeroCopy ? ", zero-copy" : "",
             cfg.width, cfg.height, settings_.frameRate.toDouble(), static_cast<double>(cfg.bitRate) / 1e6);
    std::lock_guard lock(mutex_);
    outcome_.encoder = encoder_->identity().name;
    outcome_.hardwareEncoder = encoder_->identity().hardware;
    outcome_.width = cfg.width;
    outcome_.height = cfg.height;
    return true;
}

void VideoTrackWriter::encodeSlot(std::int64_t slot, media::Frame&& frame, bool duplicate, bool keyframe) {
    if (failed_.load(std::memory_order_acquire) || !frame) return;
    if (!encoder_ && !openEncoder(frame)) return;
    frame->pts = slot;
    frame->duration = 1;
    const bool forceKey = keyframe || !firstEncoded_;
    firstEncoded_ = true;
    const AVRational tb = encoder_->timeBase();
    auto st = encoder_->encode(std::move(frame), forceKey, [&](media::Packet&& p) -> Status {
        mux_.push(std::move(p), tb);
        return ok();
    });
    if (!st) {
        setError(std::move(st).error());
        return;
    }
    framesEncoded_.fetch_add(1, std::memory_order_relaxed);
    if (duplicate) framesDuplicated_.fetch_add(1, std::memory_order_relaxed);
}

void VideoTrackWriter::run() {
    setCurrentThreadName("lectern.venc." + settings_.trackId);
    setCurrentThreadPriority(ThreadPriority::High);

    auto dup = [](const media::Frame& f) { return f.ref(); };
    using Pacer = VideoPacer<media::Frame, decltype(dup)>;
    Pacer pacer(Pacer::Config{settings_.frameRate, settings_.latencyAllowance, settings_.maxHold}, dup);
    auto emit = [this](std::int64_t slot, media::Frame&& f, bool duplicate, bool key) {
        encodeSlot(slot, std::move(f), duplicate, key);
    };

    const auto frameMs = settings_.frameRate.frameDuration().toMilliseconds();
    const auto poll = std::chrono::milliseconds(std::clamp<std::int64_t>(frameMs / 2, 2, 10));
    std::optional<CapturedVideoFrame> preroll;
    bool seeded = false;

    auto handle = [&](CapturedVideoFrame&& item) {
        const std::optional<Time> t = clock_.toSessionTime(item.hostTimeNs);
        if (!t) {
            const auto start = clock_.startHostNs();
            if (!start || item.hostTimeNs < *start) {
                preroll = std::move(item);  // newest frame before T0
                return;
            }
            const auto stopHost = clock_.stopHostNs();
            if (stopHost && item.hostTimeNs >= *stopHost) sawPostStopFrame_ = true;
            return;  // paused interval or after stop
        }
        if (resumeKeyframe_.exchange(false, std::memory_order_acq_rel)) pacer.requestKeyframe();
        pacer.push(std::move(item.frame), *t, emit);
    };

    auto seedIfNeeded = [&] {
        if (seeded || !clock_.isStarted()) return;
        seeded = true;
        if (!preroll) {
            std::lock_guard lock(seedMutex_);
            if (seed_) preroll = std::move(*seed_);
            seed_.reset();
        }
        const auto start = clock_.startHostNs();
        if (preroll && start) {
            const Time t = Time::fromNanoseconds(preroll->hostTimeNs - *start);
            pacer.seed(std::move(preroll->frame), t);
        }
        preroll.reset();
    };

    for (;;) {
        if (aborted_.load(std::memory_order_acquire)) return;
        if (std::optional<CapturedVideoFrame> item = input_.popFor(poll)) {
            if (!clock_.isStarted()) {
                preroll = std::move(*item);
                continue;
            }
            seedIfNeeded();
            handle(std::move(*item));
        }
        seedIfNeeded();
        if (seeded) pacer.advance(clock_.elapsed(), emit);

        if (failed_.load(std::memory_order_acquire)) break;
        if (stopRequested_.load(std::memory_order_acquire)) {
            const auto stopHost = clock_.stopHostNs();
            const bool timedOut =
                !stopHost || clock_.clock().nowNs() >= *stopHost + settings_.drainTimeout.toNanoseconds();
            if (sawPostStopFrame_ || timedOut) {
                while (std::optional<CapturedVideoFrame> more = input_.tryPop()) handle(std::move(*more));
                seedIfNeeded();
                if (const auto end = clock_.stopSessionTime()) pacer.finish(*end, emit);
                break;
            }
        }
    }

    const auto& c = pacer.counters();
    framesDecimated_.store(c.decimated, std::memory_order_relaxed);
    framesLate_.store(c.late, std::memory_order_relaxed);
    gapSlots_.store(c.gapSlots, std::memory_order_relaxed);
    finalize();
}

void VideoTrackWriter::finalize() {
    input_.close();
    if (encoder_ && !failed_.load(std::memory_order_acquire)) {
        const AVRational tb = encoder_->timeBase();
        auto st = encoder_->flush([&](media::Packet&& p) -> Status {
            mux_.push(std::move(p), tb);
            return ok();
        });
        if (!st) setError(std::move(st).error());
    }
    mux_.finish();
    mux_.waitFinished(std::chrono::seconds(60));
    if (auto e = mux_.error()) setError(*e);

    std::lock_guard lock(mutex_);
    outcome_.start = mux_.firstTimestamp();
    outcome_.end = mux_.endTimestamp();
    outcome_.bytes = mux_.bytesWritten();
    if (failed_.load(std::memory_order_acquire)) {
        outcome_.state = TrackState::Failed;
    } else if (mux_.packetsWritten() == 0) {
        outcome_.state = TrackState::Empty;
    } else {
        outcome_.state = TrackState::Completed;
    }
    LEC_INFO("capture", "track {} finished: {} ({} frames, {} duplicated, {} dropped, {:.1f} MB)", settings_.trackId,
             toString(outcome_.state), framesEncoded_.load(), framesDuplicated_.load(), droppedQueueFull_.load(),
             static_cast<double>(outcome_.bytes) / 1e6);
    finished_.store(true, std::memory_order_release);
    finishedCv_.notify_all();
}

VideoTrackStats VideoTrackWriter::stats() const {
    VideoTrackStats s;
    s.framesReceived = framesReceived_.load(std::memory_order_relaxed);
    s.framesEncoded = framesEncoded_.load(std::memory_order_relaxed);
    s.framesDuplicated = framesDuplicated_.load(std::memory_order_relaxed);
    s.framesDecimated = framesDecimated_.load(std::memory_order_relaxed);
    s.framesLate = framesLate_.load(std::memory_order_relaxed);
    s.droppedQueueFull = droppedQueueFull_.load(std::memory_order_relaxed);
    s.gapSlots = gapSlots_.load(std::memory_order_relaxed);
    s.inputQueueDepth = input_.size();
    std::lock_guard lock(mutex_);
    s.encoder = outcome_.encoder;
    s.hardwareEncoder = outcome_.hardwareEncoder;
    s.zeroCopy = zeroCopy_.load(std::memory_order_relaxed);
    s.width = outcome_.width;
    s.height = outcome_.height;
    return s;
}

TrackOutcome VideoTrackWriter::outcome() const {
    std::lock_guard lock(mutex_);
    return outcome_;
}

}  // namespace lectern::capture
