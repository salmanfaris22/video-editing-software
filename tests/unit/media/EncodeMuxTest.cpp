#include "media/AudioEncoder.h"
#include "media/HardwareCapabilities.h"
#include "media/MediaProbe.h"
#include "media/Muxer.h"
#include "media/SalvageRemux.h"
#include "media/VideoReader.h"
#include "media/VideoEncoder.h"
#include "support/TestSupport.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <fstream>

using namespace lectern;
using namespace lectern::media;

namespace {

Frame makeFrame(int w, int h, int index) {
    auto f = Frame::allocVideo(w, h, AV_PIX_FMT_NV12);
    EXPECT_TRUE(f);
    for (int y = 0; y < h; ++y) std::memset((*f)->data[0] + y * (*f)->linesize[0], (index * 7 + y) & 0xFF, static_cast<std::size_t>(w));
    for (int y = 0; y < h / 2; ++y) std::memset((*f)->data[1] + y * (*f)->linesize[1], 128, static_cast<std::size_t>(w));
    return std::move(*f);
}

// Writes `frames` frames of video into a Matroska file; returns frames written.
void writeVideoFile(const std::filesystem::path& path, int frames, EncoderPreference pref, bool finalize) {
    VideoEncoderConfig cfg;
    cfg.width = 320;
    cfg.height = 180;
    cfg.frameRate = FrameRate(30, 1);
    cfg.bitRate = 800'000;
    cfg.gopFrames = 30;
    cfg.preference = pref;
    auto enc = VideoEncoder::create(cfg);
    ASSERT_TRUE(enc) << enc.error().toString();
    auto mux = Muxer::create(path, recordingMuxerOptions());
    ASSERT_TRUE(mux) << mux.error().toString();
    ASSERT_TRUE((*mux)->addStream((*enc)->context()));
    ASSERT_TRUE((*mux)->writeHeader());
    const AVRational tb = (*enc)->timeBase();
    auto sink = [&](Packet&& p) { return (*mux)->write(std::move(p), 0, tb); };
    for (int i = 0; i < frames; ++i) {
        Frame f = makeFrame(320, 180, i);
        f->pts = i;
        ASSERT_TRUE((*enc)->encode(std::move(f), i == 0, sink));
    }
    ASSERT_TRUE((*enc)->flush(sink));
    if (finalize) {
        ASSERT_TRUE((*mux)->finalize());
    } else {
        ASSERT_TRUE((*mux)->flush());
        (*mux)->abandon();  // exactly what a crash leaves behind
    }
}

}  // namespace

TEST(VideoEncoder, SoftwareH264RoundTripsThroughMatroska) {
    test::TempDir dir;
    const auto path = dir / "video.mkv";
    writeVideoFile(path, 90, EncoderPreference::SoftwareOnly, true);
    auto info = probeMedia(path);
    ASSERT_TRUE(info) << info.error().toString();
    EXPECT_EQ(info->container, "matroska,webm");
    ASSERT_NE(info->video(), nullptr);
    EXPECT_EQ(info->video()->codec, "h264");
    EXPECT_EQ(info->video()->video->width, 320);
    EXPECT_NEAR(info->duration.toSecondsF(), 3.0, 0.05);
    auto decoded = test::decodeVideo(path);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->ptsSeconds.size(), 90u);
    EXPECT_TRUE(decoded->keyframes.front());
}

TEST(VideoEncoder, HardwareEncoderWhenAvailable) {
    const auto caps = HardwareCapabilities::probe(true);
    std::printf("%s\n", caps.describe().c_str());
    if (!caps.hasHardwareEncoder(VideoCodec::H264)) GTEST_SKIP() << "no hardware H.264 encoder on this machine";
    test::TempDir dir;
    writeVideoFile(dir / "hw.mkv", 60, EncoderPreference::HardwareOnly, true);
    auto decoded = test::decodeVideo(dir / "hw.mkv");
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->ptsSeconds.size(), 60u);
}

TEST(VideoEncoder, RejectsOddDimensions) {
    VideoEncoderConfig cfg;
    cfg.width = 321;
    cfg.height = 180;
    auto enc = VideoEncoder::create(cfg);
    ASSERT_FALSE(enc);
    EXPECT_EQ(enc.error().code(), ErrorCode::InvalidArgument);
}

TEST(AudioEncoder, FlacPreservesStartOffsetAndSampleCount) {
    test::TempDir dir;
    const auto path = dir / "audio.mkv";
    {
        auto enc = AudioEncoder::create({AudioCodec::Flac, 48'000, 1, 24});
        ASSERT_TRUE(enc) << enc.error().toString();
        (*enc)->setStartSample(24'000);  // track starts 0.5 s into the session
        auto mux = Muxer::create(path, recordingMuxerOptions());
        ASSERT_TRUE(mux);
        ASSERT_TRUE((*mux)->addStream((*enc)->context()));
        ASSERT_TRUE((*mux)->writeHeader());
        const AVRational tb = (*enc)->timeBase();
        auto sink = [&](Packet&& p) { return (*mux)->write(std::move(p), 0, tb); };
        std::vector<float> chunk(1000);
        for (int c = 0; c < 96; ++c) {  // 96'000 samples = 2 s
            for (int i = 0; i < 1000; ++i) chunk[i] = 0.25f * std::sin((c * 1000 + i) * 0.05f);
            ASSERT_TRUE((*enc)->encode(chunk.data(), 1000, sink));
        }
        ASSERT_TRUE((*enc)->flush(sink));
        ASSERT_TRUE((*mux)->finalize());
    }
    auto audio = test::decodeAudio(path);
    ASSERT_TRUE(audio) << audio.error().toString();
    EXPECT_NEAR(audio->startSeconds, 0.5, 0.001);
    EXPECT_EQ(audio->channel0.size(), 96'000u);
    // Lossless: decoded samples match the input within 24-bit quantization.
    EXPECT_NEAR(audio->channel0[1234], 0.25f * std::sin(1234 * 0.05f), 1e-5f);
}

TEST(Salvage, RecoversCrashTruncatedFile) {
    test::TempDir dir;
    const auto crashed = dir / "crashed.mkv";
    writeVideoFile(crashed, 150, EncoderPreference::SoftwareOnly, false);  // 5 s, no trailer
    // Additionally tear the last few KB, as an interrupted write would.
    const auto size = std::filesystem::file_size(crashed);
    std::filesystem::resize_file(crashed, size - 3000);

    const auto out = dir / "recovered.mkv";
    auto result = salvageRemux(crashed, out);
    ASSERT_TRUE(result) << result.error().toString();
    EXPECT_GT(result->packetsCopied, 100);
    EXPECT_EQ(result->firstTimestamp, Time::zero());
    // At most ~1 cluster (1 s) + the torn tail is lost.
    EXPECT_GT(result->endTimestamp.toSecondsF(), 3.5);
    auto decoded = test::decodeVideo(out);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->ptsSeconds.size(), static_cast<std::size_t>(result->packetsCopied));
    auto info = probeMedia(out);
    ASSERT_TRUE(info);
    EXPECT_NEAR(info->duration.toSecondsF(), result->endTimestamp.toSecondsF(), 0.05);
}

TEST(Salvage, RejectsGarbage) {
    test::TempDir dir;
    std::ofstream(dir / "garbage.mkv") << "this is not a media file";
    auto r = salvageRemux(dir / "garbage.mkv", dir / "out.mkv");
    EXPECT_FALSE(r);
}

TEST(VideoEncoder, RgbInputIsConvertedWithTheEncodersMatrix) {
    // Regression: the encoder's own RGB → YUV conversion used swscale's
    // BT.601 default while tagging the stream BT.709 (shifted colors).
    test::TempDir dir;
    const auto path = dir / "rgb.mkv";
    VideoEncoderConfig cfg;
    cfg.width = 64;
    cfg.height = 64;
    cfg.frameRate = FrameRate(30, 1);
    cfg.bitRate = 2'000'000;
    cfg.gopFrames = 10;
    cfg.preference = EncoderPreference::SoftwareOnly;
    cfg.inputFormat = AV_PIX_FMT_BGRA;
    auto enc = VideoEncoder::create(cfg);
    ASSERT_TRUE(enc) << enc.error().toString();
    auto mux = Muxer::create(path, recordingMuxerOptions());
    ASSERT_TRUE(mux);
    ASSERT_TRUE((*mux)->addStream((*enc)->context()));
    ASSERT_TRUE((*mux)->writeHeader());
    const AVRational tb = (*enc)->timeBase();
    auto sink = [&](Packet&& p) { return (*mux)->write(std::move(p), 0, tb); };
    for (int i = 0; i < 10; ++i) {
        auto f = Frame::allocVideo(64, 64, AV_PIX_FMT_BGRA);
        ASSERT_TRUE(f);
        for (int y = 0; y < 64; ++y) {
            std::uint8_t* row = (*f)->data[0] + static_cast<std::ptrdiff_t>(y) * (*f)->linesize[0];
            for (int x = 0; x < 64; ++x) {
                row[4 * x + 0] = 40;   // B
                row[4 * x + 1] = 60;   // G
                row[4 * x + 2] = 200;  // R
                row[4 * x + 3] = 255;
            }
        }
        (*f)->pts = i;
        ASSERT_TRUE((*enc)->encode(std::move(*f), i == 0, sink));
    }
    ASSERT_TRUE((*enc)->flush(sink));
    ASSERT_TRUE((*mux)->finalize());
    auto reader = VideoReader::open(path, {.hardwareDecode = false});
    ASSERT_TRUE(reader);
    auto frame = (*reader)->frameAt(Time::fromMilliseconds(100));
    ASSERT_TRUE(frame);
    const int luma = (*frame)->data[0][static_cast<std::ptrdiff_t>(32) * (*frame)->linesize[0] + 32];
    // BT.709 limited range: Y = 16 + 219·(0.2126·R + 0.7152·G + 0.0722·B)/255 ≈ 92 (BT.601 would give ≈ 101).
    EXPECT_NEAR(luma, 92, 3);
}
