#pragma once

#include "media/FFmpeg.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace lectern::media {

struct FileSinkOptions {
    /// AVIO buffer size. Data in this buffer is lost on an app crash until
    /// flushBuffer() is called, which the mux thread does at least every 500 ms.
    int bufferSize = 256 * 1024;
};

/// File output for libavformat through a custom AVIOContext.
///
/// Owning the file descriptor (instead of letting FFmpeg's file protocol do
/// it) gives us: fsync/F_FULLFSYNC for durability, exact byte accounting for
/// disk-space prediction, and errno capture so ENOSPC surfaces as
/// ErrorCode::OutOfSpace instead of a generic I/O error.
class FileSink {
public:
    static Result<std::unique_ptr<FileSink>> create(const std::filesystem::path& path,
                                                    const FileSinkOptions& options = {});
    ~FileSink();
    FileSink(const FileSink&) = delete;
    FileSink& operator=(const FileSink&) = delete;

    [[nodiscard]] AVIOContext* avio() const noexcept { return avio_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    /// Logical file size (highest byte written), safe to read from any thread.
    [[nodiscard]] std::uint64_t bytesWritten() const noexcept { return size_.load(std::memory_order_relaxed); }
    /// errno of the last failed write/seek, 0 if none.
    [[nodiscard]] int lastErrno() const noexcept { return lastErrno_.load(std::memory_order_relaxed); }

    /// Pushes the AVIO buffer to the kernel (survives an application crash).
    Status flushBuffer();
    /// fsync (full=true: F_FULLFSYNC on macOS; survives OS crash / power loss).
    Status sync(bool full);
    /// Flushes, syncs (if requested) and closes. Idempotent.
    Status close(bool syncToDisk = true);

private:
    FileSink() = default;
    static int writePacket(void* opaque, const std::uint8_t* buf, int size);
    static std::int64_t seek(void* opaque, std::int64_t offset, int whence);

    std::filesystem::path path_;
    int fd_ = -1;
    AVIOContext* avio_ = nullptr;
    std::int64_t position_ = 0;
    std::atomic<std::uint64_t> size_{0};
    std::atomic<int> lastErrno_{0};
};

}  // namespace lectern::media
