#include "support/TestMedia.h"

#include "media/AudioEncoder.h"
#include "media/Muxer.h"
#include "media/VideoEncoder.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <cstring>
#include <vector>

namespace lectern::test {

using namespace lectern::media;

Status writeTestVideo(const std::filesystem::path& path, const TestVideoSpec& spec) {
    VideoEncoderConfig cfg;
    cfg.width = spec.width;
    cfg.height = spec.height;
    cfg.frameRate = FrameRate(spec.fps, 1);
    cfg.bitRate = 1'500'000;
    cfg.gopFrames = spec.gopFrames;
    cfg.preference = EncoderPreference::SoftwareOnly;  // deterministic across machines
    auto enc = VideoEncoder::create(cfg);
    if (!enc) return fail(std::move(enc).error());
    auto mux = Muxer::create(path, recordingMuxerOptions());
    if (!mux) return fail(std::move(mux).error());
    if (auto r = (*mux)->addStream((*enc)->context()); !r) return fail(std::move(r).error());
    LEC_TRY((*mux)->writeHeader());
    const AVRational tb = (*enc)->timeBase();
    auto sink = [&](Packet&& p) { return (*mux)->write(std::move(p), 0, tb); };
    const auto first = static_cast<std::int64_t>(std::llround(spec.startSeconds * spec.fps));
    const auto count = static_cast<std::int64_t>(std::llround(spec.seconds * spec.fps));
    for (std::int64_t i = 0; i < count; ++i) {
        auto f = Frame::allocVideo(spec.width, spec.height, AV_PIX_FMT_NV12);
        if (!f) return fail(std::move(f).error());
        const std::int64_t index = first + i;  // content follows media time
        for (int y = 0; y < spec.height; ++y) {
            std::memset((*f)->data[0] + static_cast<std::ptrdiff_t>(y) * (*f)->linesize[0], frameLevel(index),
                        static_cast<std::size_t>(spec.width));
        }
        for (int y = 0; y < spec.height / 2; ++y) {
            std::uint8_t* row = (*f)->data[1] + static_cast<std::ptrdiff_t>(y) * (*f)->linesize[1];
            for (int x = 0; x < spec.width / 2; ++x) {
                row[2 * x] = static_cast<std::uint8_t>(spec.u);
                row[2 * x + 1] = static_cast<std::uint8_t>(spec.v);
            }
        }
        (*f)->pts = index;
        LEC_TRY((*enc)->encode(std::move(*f), i % spec.gopFrames == 0, sink));
    }
    LEC_TRY((*enc)->flush(sink));
    return (*mux)->finalize();
}

Status writeTestHdrVideo(const std::filesystem::path& path, const TestHdrVideoSpec& spec) {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_FFV1);
    if (!codec) return fail(ErrorCode::EncoderError, "FFV1 encoder not available");
    std::unique_ptr<AVCodecContext, void (*)(AVCodecContext*)> ctx(avcodec_alloc_context3(codec), [](AVCodecContext* c) {
        avcodec_free_context(&c);
    });
    ctx->width = spec.width;
    ctx->height = spec.height;
    ctx->pix_fmt = AV_PIX_FMT_YUV420P10LE;
    ctx->time_base = {1, spec.fps};
    ctx->framerate = {spec.fps, 1};
    ctx->color_primaries = AVCOL_PRI_BT2020;
    ctx->colorspace = AVCOL_SPC_BT2020_NCL;
    ctx->color_range = AVCOL_RANGE_MPEG;
    ctx->color_trc = std::string_view(spec.transfer) == "smpte2084" ? AVCOL_TRC_SMPTE2084 : AVCOL_TRC_ARIB_STD_B67;
    ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(ctx.get(), codec, nullptr) < 0) return fail(ErrorCode::EncoderError, "cannot open FFV1");
    auto mux = Muxer::create(path, recordingMuxerOptions());
    if (!mux) return fail(std::move(mux).error());
    if (auto r = (*mux)->addStream(*ctx); !r) return fail(std::move(r).error());
    LEC_TRY((*mux)->writeHeader());
    auto drain = [&]() -> Status {
        for (;;) {
            Packet pkt = Packet::alloc();
            const int rc = avcodec_receive_packet(ctx.get(), pkt.get());
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) return ok();
            if (rc < 0) return fail(ErrorCode::EncoderError, "FFV1 encode failed");
            LEC_TRY((*mux)->write(std::move(pkt), 0, ctx->time_base));
        }
    };
    const auto count = static_cast<std::int64_t>(std::llround(spec.seconds * spec.fps));
    for (std::int64_t i = 0; i < count; ++i) {
        auto f = Frame::allocVideo(spec.width, spec.height, AV_PIX_FMT_YUV420P10LE);
        if (!f) return fail(std::move(f).error());
        for (int plane = 0; plane < 3; ++plane) {
            const int w = plane == 0 ? spec.width : spec.width / 2;
            const int h = plane == 0 ? spec.height : spec.height / 2;
            const auto value = static_cast<std::uint16_t>(plane == 0 ? spec.luma : 512);
            for (int y = 0; y < h; ++y) {
                auto* row = reinterpret_cast<std::uint16_t*>((*f)->data[plane] + static_cast<std::ptrdiff_t>(y) * (*f)->linesize[plane]);
                std::fill(row, row + w, value);
            }
        }
        (*f)->pts = i;
        (*f)->color_primaries = ctx->color_primaries;
        (*f)->color_trc = ctx->color_trc;
        (*f)->colorspace = ctx->colorspace;
        (*f)->color_range = ctx->color_range;
        if (avcodec_send_frame(ctx.get(), f->get()) < 0) return fail(ErrorCode::EncoderError, "FFV1 encode failed");
        LEC_TRY(drain());
    }
    avcodec_send_frame(ctx.get(), nullptr);
    LEC_TRY(drain());
    return (*mux)->finalize();
}

Status writeTestAudio(const std::filesystem::path& path, const TestAudioSpec& spec) {
    auto enc = AudioEncoder::create({AudioCodec::Flac, spec.sampleRate, spec.channels, 24});
    if (!enc) return fail(std::move(enc).error());
    const auto first = static_cast<std::int64_t>(std::llround(spec.startSeconds * spec.sampleRate));
    (*enc)->setStartSample(first);
    auto mux = Muxer::create(path, recordingMuxerOptions());
    if (!mux) return fail(std::move(mux).error());
    if (auto r = (*mux)->addStream((*enc)->context()); !r) return fail(std::move(r).error());
    LEC_TRY((*mux)->writeHeader());
    const AVRational tb = (*enc)->timeBase();
    auto sink = [&](Packet&& p) { return (*mux)->write(std::move(p), 0, tb); };
    const auto total = static_cast<std::int64_t>(std::llround(spec.seconds * spec.sampleRate));
    constexpr int kChunk = 1024;
    std::vector<float> chunk(static_cast<std::size_t>(kChunk * spec.channels));
    for (std::int64_t done = 0; done < total;) {
        const int n = static_cast<int>(std::min<std::int64_t>(kChunk, total - done));
        for (int i = 0; i < n; ++i) {
            const std::int64_t sample = first + done + i;
            const float env = spec.envelope ? spec.envelope(static_cast<double>(sample) / spec.sampleRate) : 1.0f;
            const float v = spec.gain * env * rampSample(sample);
            for (int c = 0; c < spec.channels; ++c) chunk[static_cast<std::size_t>(i * spec.channels + c)] = v;
        }
        LEC_TRY((*enc)->encode(chunk.data(), n, sink));
        done += n;
    }
    LEC_TRY((*enc)->flush(sink));
    return (*mux)->finalize();
}

}  // namespace lectern::test
