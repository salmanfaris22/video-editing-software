#pragma once

#include "media/FileSink.h"
#include "media/Frame.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace lectern::media {

struct MuxerOptions {
    std::string format = "matroska";
    /// Muxer private options, e.g. {"cluster_time_limit", "1000"}.
    std::vector<std::pair<std::string, std::string>> formatOptions;
    /// Container-level metadata tags (title, encoder, creation_time, ...).
    std::vector<std::pair<std::string, std::string>> metadata;
    FileSinkOptions sink;
};

/// Crash-safe defaults for recording (docs/RECORDING_ENGINE.md §7): 1 s / 4 MB
/// Matroska clusters bound what an unfinished cluster can lose; CRC-32 per
/// cluster lets salvage detect torn writes.
[[nodiscard]] MuxerOptions recordingMuxerOptions();

/// One output file written through FileSink.
///
/// Lifecycle: create → addStream… → writeHeader → write… → finalize (or
/// abandon). Not thread-safe; owned by a single mux thread.
class Muxer {
public:
    static Result<std::unique_ptr<Muxer>> create(const std::filesystem::path& path, const MuxerOptions& options);
    ~Muxer();
    Muxer(const Muxer&) = delete;
    Muxer& operator=(const Muxer&) = delete;

    /// Adds a stream configured from an opened encoder.
    Result<int> addStream(const AVCodecContext& encoder);
    /// Adds a stream from codec parameters (remuxing).
    Result<int> addStream(const AVCodecParameters& parameters, AVRational timeBase);

    Status writeHeader();
    /// Writes a packet whose timestamps are in `sourceTimeBase`.
    Status write(Packet&& packet, int streamIndex, AVRational sourceTimeBase);
    /// Pushes buffered bytes to the kernel (app-crash safety).
    Status flush();
    /// fsync / F_FULLFSYNC (OS-crash safety).
    Status sync(bool full);
    /// Writes the trailer (Cues, duration), syncs and closes. Idempotent.
    Status finalize();
    /// Closes without a trailer, leaving the file exactly as a crash would.
    void abandon();

    [[nodiscard]] bool headerWritten() const noexcept { return headerWritten_; }
    [[nodiscard]] std::uint64_t bytesWritten() const noexcept { return sink_ ? sink_->bytesWritten() : finalBytes_; }
    [[nodiscard]] AVRational streamTimeBase(int index) const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    Muxer() = default;
    Error mapWriteError(int ret, std::string_view what) const;

    std::filesystem::path path_;
    MuxerOptions options_;
    std::unique_ptr<FileSink> sink_;
    AVFormatContext* format_ = nullptr;
    bool headerWritten_ = false;
    bool closed_ = false;
    std::uint64_t finalBytes_ = 0;
};

}  // namespace lectern::media
