#include "media/Frame.h"

namespace lectern::media {

Result<Frame> Frame::allocVideo(int width, int height, AVPixelFormat format) {
    Frame f = alloc();
    if (!f) return fail(ErrorCode::OutOfMemory, "av_frame_alloc");
    f->width = width;
    f->height = height;
    f->format = format;
    if (const int ret = av_frame_get_buffer(f.get(), 0); ret < 0) return fail(ffError(ret, "allocate video frame"));
    return f;
}

Result<Frame> Frame::allocAudio(int samples, AVSampleFormat format, const AVChannelLayout& layout, int sampleRate) {
    Frame f = alloc();
    if (!f) return fail(ErrorCode::OutOfMemory, "av_frame_alloc");
    f->nb_samples = samples;
    f->format = format;
    f->sample_rate = sampleRate;
    if (const int ret = av_channel_layout_copy(&f->ch_layout, &layout); ret < 0) {
        return fail(ffError(ret, "copy channel layout"));
    }
    if (const int ret = av_frame_get_buffer(f.get(), 0); ret < 0) return fail(ffError(ret, "allocate audio frame"));
    return f;
}

Frame Frame::ref() const noexcept {
    if (!frame_) return {};
    Frame copy = alloc();
    if (!copy) return {};
    if (av_frame_ref(copy.get(), frame_.get()) < 0) return {};
    return copy;
}

Packet Packet::ref() const noexcept {
    if (!packet_) return {};
    Packet copy = alloc();
    if (!copy) return {};
    if (av_packet_ref(copy.get(), packet_.get()) < 0) return {};
    return copy;
}

}  // namespace lectern::media
