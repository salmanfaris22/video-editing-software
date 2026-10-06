#include "media/VideoFramePool.h"

namespace lectern::media {

namespace {
constexpr int kAlign = 64;  // SIMD-friendly rows for swscale and encoders

constexpr std::size_t alignUp(std::size_t v) noexcept { return (v + kAlign - 1) / kAlign * kAlign; }
}  // namespace

Result<std::unique_ptr<VideoFramePool>> VideoFramePool::create(int width, int height, AVPixelFormat format) {
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(format);
    if (width <= 0 || height <= 0 || !desc || (desc->flags & (AV_PIX_FMT_FLAG_HWACCEL | AV_PIX_FMT_FLAG_PAL))) {
        return fail(ErrorCode::InvalidArgument, "frame pool needs a positive size and a CPU pixel format, got " +
                                                    std::to_string(width) + "x" + std::to_string(height) + " " +
                                                    pixelFormatName(format));
    }
    std::unique_ptr<VideoFramePool> p(new VideoFramePool());
    p->width_ = width;
    p->height_ = height;
    p->format_ = format;

    if (const int ret = av_image_fill_linesizes(p->linesize_.data(), format, width); ret < 0) {
        return fail(ffError(ret, "frame pool linesizes"));
    }
    std::array<ptrdiff_t, 4> linesizes{};
    for (std::size_t i = 0; i < 4; ++i) {
        p->linesize_[i] = static_cast<int>(alignUp(static_cast<std::size_t>(p->linesize_[i])));
        linesizes[i] = p->linesize_[i];
    }
    std::array<std::size_t, 4> planeSizes{};
    if (const int ret = av_image_fill_plane_sizes(planeSizes.data(), format, height, linesizes.data()); ret < 0) {
        return fail(ffError(ret, "frame pool plane sizes"));
    }
    std::size_t total = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        p->offset_[i] = total;
        total += alignUp(planeSizes[i]);
    }
    total += kAlign;  // over-read slack for SIMD code that reads past the last row

    p->pool_ = av_buffer_pool_init(total, av_buffer_alloc);
    if (!p->pool_) return fail(ErrorCode::OutOfMemory, "av_buffer_pool_init");
    return p;
}

VideoFramePool::~VideoFramePool() {
    // Frees the pool once every outstanding buffer has been returned.
    av_buffer_pool_uninit(&pool_);
}

Result<Frame> VideoFramePool::acquire() {
    Frame f = Frame::alloc();
    if (!f) return fail(ErrorCode::OutOfMemory, "av_frame_alloc");
    AVBufferRef* buffer = av_buffer_pool_get(pool_);
    if (!buffer) return fail(ErrorCode::OutOfMemory, "frame pool exhausted");
    f->buf[0] = buffer;  // owned by the frame from here on
    for (std::size_t i = 0; i < 4 && linesize_[i] > 0; ++i) {
        f->data[i] = buffer->data + offset_[i];
        f->linesize[i] = linesize_[i];
    }
    f->width = width_;
    f->height = height_;
    f->format = format_;
    return f;
}

}  // namespace lectern::media
