#pragma once

#include "media/Frame.h"

#include <array>
#include <cstddef>
#include <memory>

namespace lectern::media {

/// Recycles CPU buffers for video frames of one size and pixel format.
///
/// Capture paths that land frames in system memory (Windows GPU readback,
/// Media Foundation cameras, V4L2) would otherwise allocate — and page-fault —
/// a fresh multi-megabyte buffer for every frame. Buffers return to the pool
/// when the last frame referencing them is released; frames may outlive the
/// pool. Thread-safe (backed by AVBufferPool).
class VideoFramePool {
public:
    [[nodiscard]] static Result<std::unique_ptr<VideoFramePool>> create(int width, int height, AVPixelFormat format);
    ~VideoFramePool();
    VideoFramePool(const VideoFramePool&) = delete;
    VideoFramePool& operator=(const VideoFramePool&) = delete;

    /// A writable frame on a recycled buffer: one allocation holding all
    /// planes, each linesize a multiple of 64 bytes.
    [[nodiscard]] Result<Frame> acquire();

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] AVPixelFormat format() const noexcept { return format_; }
    [[nodiscard]] bool matches(int width, int height, AVPixelFormat format) const noexcept {
        return width == width_ && height == height_ && format == format_;
    }

private:
    VideoFramePool() = default;

    AVBufferPool* pool_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    AVPixelFormat format_ = AV_PIX_FMT_NONE;
    std::array<int, 4> linesize_{};
    std::array<std::size_t, 4> offset_{};
};

}  // namespace lectern::media
