#include "support/TestMedia.h"

#include "media/AudioEncoder.h"
#include "media/Muxer.h"
#include "media/VideoEncoder.h"

#include <cmath>
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
