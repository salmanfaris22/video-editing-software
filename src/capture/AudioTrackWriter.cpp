#include "capture/TrackWriters.h"

#include "core/Log.h"

#include <algorithm>
#include <cmath>

namespace lectern::capture {

namespace {
audio::DriftController::Config driftConfig(const AudioTrackSettings& s) {
    audio::DriftController::Config c;
    c.outputRate = s.outputSampleRate;
    c.hardThresholdSamples = s.hardThresholdMs * s.outputSampleRate / 1000.0;
    c.compensationDistance = s.outputSampleRate;
    c.maxRatio = s.maxCompensationRatio;
    return c;
}
}  // namespace

AudioTrackWriter::AudioTrackWriter(AudioTrackSettings settings, const SessionClock& clock, int sourceChannels)
    : settings_(std::move(settings)),
      clock_(clock),
      outChannels_(std::clamp(sourceChannels, 1, std::max(1, settings_.maxChannels))),
      mux_(settings_.trackId, settings_.outputPath, settings_.muxer, settings_.mux),
      drift_(driftConfig(settings_)),
      delay_(outChannels_, static_cast<int>(settings_.delayLine.toSamples(settings_.outputSampleRate)),
             static_cast<int>(settings_.fadeDuration.toSamples(settings_.outputSampleRate))) {
    emit_ = [this](const float* data, int frames) { emitToEncoder(data, frames); };
    outcome_.sampleRate = settings_.outputSampleRate;
    outcome_.channels = outChannels_;
    outcome_.codec = std::string(media::toString(settings_.codec));
}

AudioTrackWriter::~AudioTrackWriter() = default;

Status AudioTrackWriter::open() {
    media::AudioEncoderConfig cfg;
    cfg.codec = settings_.codec;
    cfg.sampleRate = settings_.outputSampleRate;
    cfg.channels = outChannels_;
    cfg.bitsPerSample = settings_.bitsPerSample;
    cfg.bitRate = settings_.bitRate;
    auto enc = media::AudioEncoder::create(cfg);
    if (!enc) return fail(std::move(enc).error().withContext("track " + settings_.trackId));
    encoder_ = std::move(*enc);
    LEC_TRY(mux_.open());
    LEC_TRY(mux_.configure(encoder_->context()));
    std::lock_guard lock(mutex_);
    outcome_.encoder = encoder_->name();
    outcome_.state = TrackState::Recording;
    return ok();
}

void AudioTrackWriter::setError(Error e) {
    LEC_ERROR("capture", "track {}: {}", settings_.trackId, e.toString());
    std::lock_guard lock(mutex_);
    if (!outcome_.error) outcome_.error = std::move(e);
    failed_.store(true, std::memory_order_release);
}

void AudioTrackWriter::audioSourceEvent(const SourceEvent& event) noexcept {
    if (event.type == SourceEvent::Type::Error || event.type == SourceEvent::Type::Interrupted) {
        LEC_WARN("capture", "track {}: source {}: {}", settings_.trackId, toString(event.type), event.message);
    }
}

Status AudioTrackWriter::ensureResampler(int inputRate) {
    if (resampler_ && inputRate_ == inputRate) return ok();
    if (resampler_) {
        // Device rate changed mid-stream: flush the old resampler so no
        // samples are lost, then continue with a new one.
        const int cap = 8192;
        resampled_.resize(static_cast<std::size_t>(cap * outChannels_));
        if (auto n = resampler_->drain(resampled_.data(), cap); n && *n > 0) {
            written_ += *n;
            framesOut_.fetch_add(static_cast<std::uint64_t>(*n), std::memory_order_relaxed);
            delay_.push(resampled_.data(), *n, emit_);
        }
        LEC_INFO("capture", "track {}: input rate changed {} → {} Hz", settings_.trackId, inputRate_, inputRate);
    }
    auto r = media::AudioResampler::create(
        {inputRate, outChannels_, settings_.outputSampleRate, outChannels_, /*forceResampling=*/true});
    if (!r) return fail(std::move(r).error());
    resampler_ = std::move(*r);
    inputRate_ = inputRate;
    audio::TimestampSmoother::Config sc;
    sc.nominalRate = inputRate;
    smoother_ = std::make_unique<audio::TimestampSmoother>(sc);
    return ok();
}

void AudioTrackWriter::mapChannels(const AudioBlock& block) {
    const std::size_t frames = static_cast<std::size_t>(block.frames);
    const auto out = static_cast<std::size_t>(outChannels_);
    const auto in = static_cast<std::size_t>(block.channels);
    mapped_.resize(frames * out);
    float* dst = mapped_.data();
    const float* src = block.interleaved;
    if (in == out) {
        std::copy(src, src + frames * in, dst);
    } else if (in == 1) {  // mono → N: duplicate
        for (std::size_t i = 0; i < frames; ++i) {
            for (std::size_t c = 0; c < out; ++c) dst[i * out + c] = src[i];
        }
    } else if (out == 1) {  // N → mono: average of the first two channels
        for (std::size_t i = 0; i < frames; ++i) dst[i] = 0.5f * (src[i * in] + src[i * in + 1]);
    } else {  // N → fewer: keep the first channels (interfaces, not surround)
        for (std::size_t i = 0; i < frames; ++i) {
            for (std::size_t c = 0; c < out; ++c) dst[i * out + c] = src[i * in + std::min(c, in - 1)];
        }
    }
}

void AudioTrackWriter::consumeAudio(const AudioBlock& block) noexcept {
    try {
        processBlock(block);
    } catch (const std::exception& e) {
        setError(Error(ErrorCode::Internal, std::string("audio processing: ") + e.what()));
    }
}

void AudioTrackWriter::processBlock(const AudioBlock& block) {
    if (aborted_.load(std::memory_order_acquire) || finished_.load(std::memory_order_acquire) ||
        failed_.load(std::memory_order_acquire)) {
        return;
    }
    if (block.frames <= 0 || block.channels <= 0 || block.sampleRate <= 0) return;
    lastBlockHostNs_.store(block.hostTimeNs, std::memory_order_relaxed);
    framesIn_.fetch_add(static_cast<std::uint64_t>(block.frames), std::memory_order_relaxed);
    if (!clock_.isStarted()) return;

    if (auto st = ensureResampler(block.sampleRate); !st) {
        setError(std::move(st).error());
        return;
    }
    mapChannels(block);

    const audio::TimestampSmoother::Update u = smoother_->add(block.sampleIndex, block.hostTimeNs);
    if (u.discontinuity || block.discontinuity) discontinuities_.fetch_add(1, std::memory_order_relaxed);
    driftPpm_.store(smoother_->driftPpm(), std::memory_order_relaxed);

    const double period = smoother_->samplePeriodNs();
    const double h0 = u.smoothedHostNs;
    const double h1 = h0 + block.frames * period;
    const double frames = block.frames;
    const double outRate = settings_.outputSampleRate;

    clock_.forEachActiveSpan(
        static_cast<std::int64_t>(std::floor(h0)), static_cast<std::int64_t>(std::ceil(h1)) + 1,
        [&](const SessionClock::Span& span) {
            const double i0 = std::clamp(std::ceil((static_cast<double>(span.hostBegin) - h0) / period), 0.0, frames);
            const double i1 = std::clamp(std::ceil((static_cast<double>(span.hostEnd) - h0) / period), 0.0, frames);
            if (i1 <= i0) return;
            const double firstHost = h0 + i0 * period;
            const double sessionNs = static_cast<double>(span.sessionBegin.toNanoseconds()) +
                                     (firstHost - static_cast<double>(span.hostBegin));
            const double target = sessionNs * outRate / 1e9;
            const bool splice = started_ && std::fabs(firstHost - lastFedHostEnd_) > 2.0 * period;
            feed(mapped_.data() + static_cast<std::size_t>(i0) * static_cast<std::size_t>(outChannels_),
                 static_cast<int>(i1 - i0), target, splice);
            lastFedHostEnd_ = h0 + i1 * period;
        });

    if (const auto stopHost = clock_.stopHostNs(); stopHost && h1 >= static_cast<double>(*stopHost)) {
        reachedStop_.store(true, std::memory_order_release);
    }
}

void AudioTrackWriter::feed(const float* data, int frames, double targetPosition, bool splice) {
    if (!started_) {
        started_ = true;
        const std::int64_t startSample = std::llround(targetPosition);
        encoder_->setStartSample(startSample);
        written_ = startSample;
        delay_.armFadeIn();
        std::lock_guard lock(mutex_);
        outcome_.start = Time::fromSamples(startSample, settings_.outputSampleRate);
    } else if (splice) {
        // Pause/resume or gap: fade out what was before the cut, fade in after.
        delay_.fadeOutTail();
        delay_.armFadeIn();
    }

    const double actual = static_cast<double>(written_) + resampler_->delayOutputSamples();
    const audio::DriftController::Decision d = drift_.evaluate(targetPosition, actual);
    lastErrorMs_.store(d.errorSamples * 1000.0 / settings_.outputSampleRate, std::memory_order_relaxed);

    switch (d.action) {
        case audio::DriftController::Action::InsertSilence: {
            delay_.fadeOutTail();
            delay_.pushSilence(static_cast<int>(d.samples), emit_);
            written_ += d.samples;
            silenceInserted_.fetch_add(static_cast<std::uint64_t>(d.samples), std::memory_order_relaxed);
            delay_.armFadeIn();
            (void)resampler_->setCompensation(0, 0);
            LEC_DEBUG("capture", "track {}: inserted {} samples of silence (gap)", settings_.trackId, d.samples);
            break;
        }
        case audio::DriftController::Action::DropInput: {
            const auto dropIn = std::min<std::int64_t>(
                frames, std::llround(static_cast<double>(d.samples) * inputRate_ / settings_.outputSampleRate));
            inputDropped_.fetch_add(static_cast<std::uint64_t>(dropIn), std::memory_order_relaxed);
            data += dropIn * outChannels_;
            frames -= static_cast<int>(dropIn);
            delay_.fadeOutTail();
            delay_.armFadeIn();
            (void)resampler_->setCompensation(0, 0);
            LEC_DEBUG("capture", "track {}: dropped {} input samples (overlap)", settings_.trackId, dropIn);
            if (frames <= 0) return;
            break;
        }
        case audio::DriftController::Action::Soft:
            if (auto st = resampler_->setCompensation(static_cast<int>(d.samples), d.compensationDistance); !st) {
                setError(std::move(st).error());
                return;
            }
            break;
        case audio::DriftController::Action::None:
            break;
    }

    const int cap = resampler_->maxOutputFrames(frames);
    const std::size_t need = static_cast<std::size_t>(cap) * static_cast<std::size_t>(outChannels_);
    if (resampled_.size() < need) resampled_.resize(need);
    auto produced = resampler_->process(data, frames, resampled_.data(), cap);
    if (!produced) {
        setError(std::move(produced).error());
        return;
    }
    written_ += *produced;
    framesOut_.fetch_add(static_cast<std::uint64_t>(*produced), std::memory_order_relaxed);
    delay_.push(resampled_.data(), *produced, emit_);
}

void AudioTrackWriter::emitToEncoder(const float* data, int frames) {
    if (failed_.load(std::memory_order_acquire) || frames <= 0) return;
    const AVRational tb = encoder_->timeBase();
    auto st = encoder_->encode(data, frames, [&](media::Packet&& p) -> Status {
        mux_.push(std::move(p), tb);
        return ok();
    });
    if (!st) setError(std::move(st).error());
}

Status AudioTrackWriter::finish() {
    if (finished_.load(std::memory_order_acquire) || aborted_.load(std::memory_order_acquire)) return ok();

    if (!started_) {
        // No audio reached the session (device silent/unavailable): no file.
        mux_.abort(true);
        std::lock_guard lock(mutex_);
        outcome_.state = failed_.load() ? TrackState::Failed : TrackState::Empty;
        finished_.store(true, std::memory_order_release);
        LEC_WARN("capture", "track {}: no audio was captured", settings_.trackId);
        return ok();
    }

    if (!failed_.load(std::memory_order_acquire)) {
        const int cap = 8192;
        resampled_.resize(static_cast<std::size_t>(cap * outChannels_));
        if (auto n = resampler_->drain(resampled_.data(), cap); n && *n > 0) {
            written_ += *n;
            framesOut_.fetch_add(static_cast<std::uint64_t>(*n), std::memory_order_relaxed);
            delay_.push(resampled_.data(), *n, emit_);
        }

        // End exactly at S so every track of the session ends together.
        if (const auto end = clock_.stopSessionTime()) {
            const std::int64_t endPos = end->toSamples(settings_.outputSampleRate, Rounding::Nearest);
            if (written_ < endPos) {
                delay_.fadeOutTail();
                delay_.pushSilence(static_cast<int>(std::min<std::int64_t>(endPos - written_, INT32_MAX)), emit_);
                silenceInserted_.fetch_add(static_cast<std::uint64_t>(endPos - written_), std::memory_order_relaxed);
                written_ = endPos;
            } else if (written_ > endPos) {
                written_ -= delay_.truncateTail(static_cast<int>(std::min<std::int64_t>(written_ - endPos, INT32_MAX)));
            }
        }
        delay_.fadeOutTail();
        delay_.flush(emit_);

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
    outcome_.end = Time::fromSamples(written_, settings_.outputSampleRate);
    outcome_.bytes = mux_.bytesWritten();
    outcome_.state = failed_.load(std::memory_order_acquire) ? TrackState::Failed : TrackState::Completed;
    finished_.store(true, std::memory_order_release);
    LEC_INFO("capture",
             "track {} finished: {} (drift {:+.1f} ppm, {} hard corrections, {} silence samples, {:.1f} MB)",
             settings_.trackId, toString(outcome_.state), driftPpm_.load(), drift_.hardCorrections(),
             silenceInserted_.load(), static_cast<double>(outcome_.bytes) / 1e6);
    return outcome_.error ? Status(std::unexpected(*outcome_.error)) : ok();
}

void AudioTrackWriter::abort() {
    aborted_.store(true, std::memory_order_release);
    mux_.abort(true);
    std::lock_guard lock(mutex_);
    outcome_.state = TrackState::Failed;
    if (!outcome_.error) outcome_.error = Error(ErrorCode::Cancelled, "cancelled");
    finished_.store(true, std::memory_order_release);
}

AudioTrackStats AudioTrackWriter::stats() const {
    AudioTrackStats s;
    s.framesIn = framesIn_.load(std::memory_order_relaxed);
    s.framesOut = framesOut_.load(std::memory_order_relaxed);
    s.silenceInserted = silenceInserted_.load(std::memory_order_relaxed);
    s.inputDropped = inputDropped_.load(std::memory_order_relaxed);
    s.discontinuities = discontinuities_.load(std::memory_order_relaxed);
    s.hardCorrections = drift_.hardCorrections();
    s.driftPpm = driftPpm_.load(std::memory_order_relaxed);
    s.lastErrorMs = lastErrorMs_.load(std::memory_order_relaxed);
    s.sampleRate = settings_.outputSampleRate;
    s.channels = outChannels_;
    return s;
}

TrackOutcome AudioTrackWriter::outcome() const {
    std::lock_guard lock(mutex_);
    return outcome_;
}

}  // namespace lectern::capture
