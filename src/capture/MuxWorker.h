#pragma once

#include "core/BoundedQueue.h"
#include "core/Time.h"
#include "media/Muxer.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace lectern::capture {

/// Owns one output file and the thread that writes it
/// (docs/RECORDING_ENGINE.md §5, §7).
///
/// Encoders push packets into a byte-bounded queue; this thread writes them,
/// flushes the AVIO buffer at least every 500 ms (app-crash safety) and
/// fsyncs every 10 s (OS-crash safety). A slow or stalled disk therefore
/// never blocks an encoder directly; if the queue fills, the encoder's
/// bounded push times out and the pressure propagates back to the capture
/// queue, where frames are dropped and counted.
class MuxWorker {
public:
    struct Options {
        std::size_t queueBytes = 64ull << 20;
        std::chrono::milliseconds flushInterval{500};
        std::chrono::milliseconds syncInterval{10'000};
        /// How long an encoder may block on a full queue (disk stall) before
        /// packets are dropped. Pressure normally propagates back to the
        /// capture queue long before this, where frames degrade cleanly into
        /// duplicates.
        std::chrono::milliseconds pushTimeout{2'000};
        /// Testing seam: runs on the mux thread before each write (e.g. to
        /// simulate a stalled disk). Never set in production code.
        std::function<void()> beforeWrite;
    };

    MuxWorker(std::string trackId, std::filesystem::path path, media::MuxerOptions muxerOptions, Options options);
    ~MuxWorker();
    MuxWorker(const MuxWorker&) = delete;
    MuxWorker& operator=(const MuxWorker&) = delete;

    /// Creates the file immediately (fail fast on permissions / full disk).
    Status open();
    /// Adds the single stream, writes the header and starts the writer thread.
    Status configure(const AVCodecContext& encoder);

    /// Queues a packet whose timestamps are in `timeBase`. Returns false if
    /// the packet was dropped: the queue stayed full for pushTimeout, or the
    /// worker failed. After a drop, packets are discarded until the next
    /// keyframe so the written stream always stays decodable.
    bool push(media::Packet&& packet, AVRational timeBase);

    /// No more packets: drain, write trailer, fsync, close.
    void finish();
    /// Stops without a trailer; optionally deletes the file (cancel).
    void abort(bool deleteFile);
    bool waitFinished(std::chrono::milliseconds timeout);

    [[nodiscard]] bool isConfigured() const noexcept { return configured_.load(std::memory_order_acquire); }
    [[nodiscard]] bool hasFailed() const noexcept { return failed_.load(std::memory_order_acquire); }
    [[nodiscard]] std::optional<Error> error() const;
    [[nodiscard]] std::uint64_t bytesWritten() const noexcept { return bytes_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t packetsWritten() const noexcept { return packets_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t packetsDropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::size_t queuedBytes() const { return queue_.usedCost(); }
    /// Session-time range covered by written packets.
    [[nodiscard]] std::optional<Time> firstTimestamp() const;
    [[nodiscard]] std::optional<Time> endTimestamp() const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    struct Item {
        media::Packet packet;
        AVRational timeBase;
    };
    struct ItemCost {
        std::size_t operator()(const Item& i) const noexcept { return media::PacketCost{}(i.packet); }
    };

    void run();
    void setError(Error e);

    std::string trackId_;
    std::filesystem::path path_;
    media::MuxerOptions muxerOptions_;
    Options options_;
    std::unique_ptr<media::Muxer> muxer_;
    BoundedQueue<Item, ItemCost> queue_;
    std::thread thread_;

    std::atomic<bool> configured_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> finished_{false};
    std::atomic<bool> aborting_{false};
    bool awaitKeyframe_ = false;  // producer (encoder) thread only
    std::atomic<std::uint64_t> bytes_{0};
    std::atomic<std::uint64_t> packets_{0};
    std::atomic<std::uint64_t> dropped_{0};

    mutable std::mutex mutex_;
    std::condition_variable finishedCv_;
    std::optional<Error> error_;
    std::optional<Time> first_;
    std::optional<Time> end_;
};

}  // namespace lectern::capture
