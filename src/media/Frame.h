#pragma once

#include "media/FFmpeg.h"

#include <memory>
#include <utility>

namespace lectern::media {

// ---------------------------------------------------------------------------
// RAII deleters for FFmpeg objects.

struct AVFrameDeleter {
    void operator()(AVFrame* f) const noexcept { av_frame_free(&f); }
};
struct AVPacketDeleter {
    void operator()(AVPacket* p) const noexcept { av_packet_free(&p); }
};
struct AVCodecContextDeleter {
    void operator()(AVCodecContext* c) const noexcept { avcodec_free_context(&c); }
};
struct AVFormatInputDeleter {
    void operator()(AVFormatContext* c) const noexcept { avformat_close_input(&c); }
};
struct SwsContextDeleter {
    void operator()(SwsContext* c) const noexcept { sws_freeContext(c); }
};
struct SwrContextDeleter {
    void operator()(SwrContext* c) const noexcept { swr_free(&c); }
};
struct AVAudioFifoDeleter {
    void operator()(AVAudioFifo* f) const noexcept { av_audio_fifo_free(f); }
};

using AVCodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using AVFormatInputPtr = std::unique_ptr<AVFormatContext, AVFormatInputDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;
using AVAudioFifoPtr = std::unique_ptr<AVAudioFifo, AVAudioFifoDeleter>;

/// Owns an AVDictionary for option passing.
class Dictionary {
public:
    Dictionary() = default;
    ~Dictionary() { av_dict_free(&dict_); }
    Dictionary(const Dictionary&) = delete;
    Dictionary& operator=(const Dictionary&) = delete;
    void set(const char* key, const char* value) { av_dict_set(&dict_, key, value, 0); }
    void set(const char* key, std::int64_t value) { av_dict_set_int(&dict_, key, value, 0); }
    AVDictionary** address() noexcept { return &dict_; }
    [[nodiscard]] AVDictionary* get() const noexcept { return dict_; }

private:
    AVDictionary* dict_ = nullptr;
};

/// Shared reference to an AVBufferRef (hw device / frames contexts).
class BufferRef {
public:
    BufferRef() noexcept = default;
    /// Adopts an existing reference (no new ref taken).
    static BufferRef adopt(AVBufferRef* ref) noexcept {
        BufferRef b;
        b.ref_ = ref;
        return b;
    }
    /// Takes a new reference to `ref` (may be null).
    static BufferRef share(const AVBufferRef* ref) noexcept {
        BufferRef b;
        if (ref) b.ref_ = av_buffer_ref(ref);
        return b;
    }
    ~BufferRef() { av_buffer_unref(&ref_); }
    BufferRef(const BufferRef& o) noexcept : ref_(o.ref_ ? av_buffer_ref(o.ref_) : nullptr) {}
    BufferRef& operator=(const BufferRef& o) noexcept {
        if (this != &o) {
            av_buffer_unref(&ref_);
            ref_ = o.ref_ ? av_buffer_ref(o.ref_) : nullptr;
        }
        return *this;
    }
    BufferRef(BufferRef&& o) noexcept : ref_(std::exchange(o.ref_, nullptr)) {}
    BufferRef& operator=(BufferRef&& o) noexcept {
        if (this != &o) {
            av_buffer_unref(&ref_);
            ref_ = std::exchange(o.ref_, nullptr);
        }
        return *this;
    }
    [[nodiscard]] AVBufferRef* get() const noexcept { return ref_; }
    /// New reference for handing to FFmpeg APIs that take ownership.
    [[nodiscard]] AVBufferRef* newRef() const noexcept { return ref_ ? av_buffer_ref(ref_) : nullptr; }
    explicit operator bool() const noexcept { return ref_ != nullptr; }

private:
    AVBufferRef* ref_ = nullptr;
};

// ---------------------------------------------------------------------------

/// Move-only owner of an AVFrame. AVFrame is the engine's frame currency: it
/// models CPU planes, GPU surfaces (VideoToolbox, D3D11, VA-API) and
/// reference-counted sharing, so frames move through capture → pacing →
/// encoding without copies.
class Frame {
public:
    Frame() noexcept = default;
    explicit Frame(AVFrame* adopt) noexcept : frame_(adopt) {}

    /// Allocates an empty AVFrame (no buffers). Empty Frame on OOM.
    [[nodiscard]] static Frame alloc() noexcept { return Frame(av_frame_alloc()); }
    [[nodiscard]] static Result<Frame> allocVideo(int width, int height, AVPixelFormat format);
    [[nodiscard]] static Result<Frame> allocAudio(int samples, AVSampleFormat format, const AVChannelLayout& layout,
                                                  int sampleRate);

    /// A new Frame sharing this frame's buffers (av_frame_ref). Empty on failure.
    [[nodiscard]] Frame ref() const noexcept;

    [[nodiscard]] AVFrame* get() const noexcept { return frame_.get(); }
    AVFrame* operator->() const noexcept { return frame_.get(); }
    explicit operator bool() const noexcept { return frame_ != nullptr; }
    [[nodiscard]] AVFrame* release() noexcept { return frame_.release(); }
    void reset() noexcept { frame_.reset(); }

    [[nodiscard]] bool isHardware() const noexcept {
        return frame_ && isHardwarePixelFormat(static_cast<AVPixelFormat>(frame_->format));
    }

private:
    std::unique_ptr<AVFrame, AVFrameDeleter> frame_;
};

/// Move-only owner of an AVPacket.
class Packet {
public:
    Packet() noexcept = default;
    explicit Packet(AVPacket* adopt) noexcept : packet_(adopt) {}
    [[nodiscard]] static Packet alloc() noexcept { return Packet(av_packet_alloc()); }

    [[nodiscard]] Packet ref() const noexcept;

    [[nodiscard]] AVPacket* get() const noexcept { return packet_.get(); }
    AVPacket* operator->() const noexcept { return packet_.get(); }
    explicit operator bool() const noexcept { return packet_ != nullptr; }
    [[nodiscard]] int size() const noexcept { return packet_ ? packet_->size : 0; }

private:
    std::unique_ptr<AVPacket, AVPacketDeleter> packet_;
};

/// Byte cost of a packet for byte-bounded queues.
struct PacketCost {
    std::size_t operator()(const Packet& p) const noexcept { return static_cast<std::size_t>(p.size()) + 128; }
};

}  // namespace lectern::media
