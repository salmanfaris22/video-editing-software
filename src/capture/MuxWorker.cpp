#include "capture/MuxWorker.h"

#include "core/Log.h"
#include "core/Thread.h"

namespace lectern::capture {

MuxWorker::MuxWorker(std::string trackId, std::filesystem::path path, media::MuxerOptions muxerOptions,
                     Options options)
    : trackId_(std::move(trackId)),
      path_(std::move(path)),
      muxerOptions_(std::move(muxerOptions)),
      options_(options),
      queue_(options.queueBytes) {}

MuxWorker::~MuxWorker() {
    if (thread_.joinable()) {
        queue_.close();
        thread_.join();
    }
    if (muxer_) muxer_->abandon();
}

Status MuxWorker::open() {
    auto m = media::Muxer::create(path_, muxerOptions_);
    if (!m) {
        setError(m.error());
        return fail(std::move(m).error().withContext("track " + trackId_));
    }
    muxer_ = std::move(*m);
    return ok();
}

Status MuxWorker::configure(const AVCodecContext& encoder) {
    if (!muxer_) return fail(ErrorCode::InvalidState, "mux worker not open");
    auto idx = muxer_->addStream(encoder);
    if (!idx) {
        setError(idx.error());
        return fail(std::move(idx).error());
    }
    if (auto st = muxer_->writeHeader(); !st) {
        setError(st.error());
        return st;
    }
    configured_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { run(); });
    return ok();
}

bool MuxWorker::push(media::Packet&& packet, AVRational timeBase) {
    if (failed_.load(std::memory_order_acquire)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const bool keyframe = packet && (packet->flags & AV_PKT_FLAG_KEY);
    if (awaitKeyframe_ && !keyframe) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;  // dependent frame of a dropped GOP: would not decode
    }
    Item item{std::move(packet), timeBase};
    const PushResult r = queue_.pushFor(item, options_.pushTimeout);
    if (r != PushResult::Ok) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        if (!awaitKeyframe_) {
            LEC_WARN("capture", "track {}: disk stalled for {} ms, dropping until the next keyframe", trackId_,
                     options_.pushTimeout.count());
        }
        awaitKeyframe_ = true;
        return false;
    }
    awaitKeyframe_ = false;
    return true;
}

void MuxWorker::run() {
    setCurrentThreadName("lectern.mux." + trackId_);
    setCurrentThreadPriority(ThreadPriority::High);
    using Clock = std::chrono::steady_clock;
    auto lastFlush = Clock::now();
    auto lastSync = Clock::now();

    for (;;) {
        std::optional<Item> item = queue_.popFor(std::chrono::milliseconds(100));
        if (item && !failed_.load(std::memory_order_acquire)) {
            if (options_.beforeWrite) options_.beforeWrite();
            AVPacket* pkt = item->packet.get();
            const Rational tb = media::fromAV(item->timeBase);
            if (pkt->pts != AV_NOPTS_VALUE) {
                const Time pts = Time::fromRational(pkt->pts, tb);
                const Time end = pts + Time::fromRational(pkt->duration, tb);
                std::lock_guard lock(mutex_);
                if (!first_ || pts < *first_) first_ = pts;
                if (!end_ || end > *end_) end_ = end;
            }
            if (auto st = muxer_->write(std::move(item->packet), 0, item->timeBase); !st) {
                LEC_ERROR("capture", "track {}: write failed: {}", trackId_, st.error().toString());
                setError(st.error());
                queue_.clear();
            } else {
                packets_.fetch_add(1, std::memory_order_relaxed);
                bytes_.store(muxer_->bytesWritten(), std::memory_order_relaxed);
            }
        } else if (!item && queue_.isClosed()) {
            break;
        }

        const auto now = Clock::now();
        if (!failed_.load(std::memory_order_acquire) && now - lastFlush >= options_.flushInterval) {
            if (auto st = muxer_->flush(); !st) setError(st.error());
            lastFlush = now;
        }
        if (!failed_.load(std::memory_order_acquire) && now - lastSync >= options_.syncInterval) {
            if (auto st = muxer_->sync(false); !st) setError(st.error());
            lastSync = now;
        }
    }

    if (!aborting_.load(std::memory_order_acquire)) {
        // Closed via finish(): write trailer and close.
        if (auto st = muxer_->finalize(); !st) {
            LEC_ERROR("capture", "track {}: finalize failed: {}", trackId_, st.error().toString());
            setError(st.error());
        }
        bytes_.store(muxer_->bytesWritten(), std::memory_order_relaxed);
    }
    {
        std::lock_guard lock(mutex_);
        finished_.store(true, std::memory_order_release);
    }
    finishedCv_.notify_all();
}

void MuxWorker::finish() {
    if (!configured_.load(std::memory_order_acquire)) {
        // Never configured (no media arrived): close the empty file.
        if (muxer_) muxer_->abandon();
        std::lock_guard lock(mutex_);
        finished_.store(true, std::memory_order_release);
        finishedCv_.notify_all();
        return;
    }
    queue_.close();
}

void MuxWorker::abort(bool deleteFile) {
    aborting_.store(true, std::memory_order_release);
    queue_.clear();
    queue_.close();
    if (thread_.joinable()) thread_.join();
    if (muxer_) muxer_->abandon();
    if (deleteFile) {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    std::lock_guard lock(mutex_);
    finished_.store(true, std::memory_order_release);
    finishedCv_.notify_all();
}

bool MuxWorker::waitFinished(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    const bool done =
        finishedCv_.wait_for(lock, timeout, [this] { return finished_.load(std::memory_order_acquire); });
    if (done && thread_.joinable() && std::this_thread::get_id() != thread_.get_id()) {
        lock.unlock();
        thread_.join();
    }
    return done;
}

void MuxWorker::setError(Error e) {
    std::lock_guard lock(mutex_);
    if (!error_) error_ = std::move(e);
    failed_.store(true, std::memory_order_release);
}

std::optional<Error> MuxWorker::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

std::optional<Time> MuxWorker::firstTimestamp() const {
    std::lock_guard lock(mutex_);
    return first_;
}

std::optional<Time> MuxWorker::endTimestamp() const {
    std::lock_guard lock(mutex_);
    return end_;
}

}  // namespace lectern::capture
