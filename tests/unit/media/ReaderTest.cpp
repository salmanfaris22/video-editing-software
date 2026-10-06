#include "media/AudioReader.h"
#include "media/VideoReader.h"
#include "media/Waveform.h"
#include "support/TestMedia.h"
#include "support/TestSupport.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace lectern;
using namespace lectern::media;

namespace {

int centerLuma(const Frame& f) {
    const AVFrame* av = f.get();
    // NV12/yuv420p/… all start with a full-resolution luma plane.
    return av->data[0][static_cast<std::ptrdiff_t>(av->height / 2) * av->linesize[0] + av->width / 2];
}

std::int64_t frameIndexAt(double t) { return static_cast<std::int64_t>(std::floor(t * 30.0 + 1e-6)); }

class VideoReaderTest : public ::testing::TestWithParam<bool> {};

}  // namespace

TEST_P(VideoReaderTest, ReturnsTheFrameOnScreenAtAnyTime) {
    test::TempDir dir;
    const auto path = dir / "video.mkv";
    ASSERT_TRUE(test::writeTestVideo(path, {.seconds = 3.0, .startSeconds = 0.5}));
    auto reader = VideoReader::open(path, {.hardwareDecode = GetParam()});
    ASSERT_TRUE(reader) << reader.error().toString();
    EXPECT_EQ((*reader)->width(), 320);
    EXPECT_NEAR((*reader)->start().toSecondsF(), 0.5, 0.001);
    EXPECT_NEAR((*reader)->duration().toSecondsF(), 3.0, 0.05);

    // Forward playback at 60 Hz requests (each frame shown twice), then jumps.
    for (double t = 0.5; t < 3.4; t += 1.0 / 60) {
        auto f = (*reader)->frameAt(Time::fromSecondsF(t));
        ASSERT_TRUE(f) << f.error().toString();
        EXPECT_NEAR(centerLuma(*f), test::frameLevel(frameIndexAt(t)), 3) << "t=" << t;
    }
    EXPECT_EQ((*reader)->seeks(), 1u);  // one initial positioning, then pure forward decoding
    for (double t : {2.0, 0.9, 3.2, 0.5, 1.734}) {
        auto f = (*reader)->frameAt(Time::fromSecondsF(t));
        ASSERT_TRUE(f);
        EXPECT_NEAR(centerLuma(*f), test::frameLevel(frameIndexAt(t)), 3) << "t=" << t;
    }
    // Before the first frame: the first frame; after the last: the last one.
    auto early = (*reader)->frameAt(Time::fromSecondsF(0.1));
    ASSERT_TRUE(early);
    EXPECT_NEAR(centerLuma(*early), test::frameLevel(15), 3);
    auto late = (*reader)->frameAt(Time::fromSecondsF(9.0));
    ASSERT_TRUE(late);
    EXPECT_NEAR(centerLuma(*late), test::frameLevel(15 + 89), 3);
    const auto seeksAfterEnd = (*reader)->seeks();
    ASSERT_TRUE((*reader)->frameAt(Time::fromSecondsF(9.5)));
    EXPECT_EQ((*reader)->seeks(), seeksAfterEnd);  // no re-reading past the end
}

INSTANTIATE_TEST_SUITE_P(Decoders, VideoReaderTest, ::testing::Values(false, true),
                         [](const auto& info) { return info.param ? "Hardware" : "Software"; });

TEST(VideoReader, MissingFileAndAudioOnlyFileFail) {
    test::TempDir dir;
    EXPECT_EQ(VideoReader::open(dir / "missing.mkv").error().code(), ErrorCode::NotFound);
    ASSERT_TRUE(test::writeTestAudio(dir / "audio.mkv", {.seconds = 0.5}));
    EXPECT_FALSE(VideoReader::open(dir / "audio.mkv"));
}

TEST(AudioReader, SampleExactWindowsWithSilenceOutsideTheMedia) {
    test::TempDir dir;
    const auto path = dir / "mic.mkv";
    ASSERT_TRUE(test::writeTestAudio(path, {.seconds = 3.0, .channels = 1, .startSeconds = 0.25}));
    auto reader = AudioReader::open(path);
    ASSERT_TRUE(reader) << reader.error().toString();
    EXPECT_EQ((*reader)->sourceChannels(), 1);
    EXPECT_NEAR((*reader)->start().toSecondsF(), 0.25, 1e-6);

    std::vector<float> buf(4800 * 2);
    // [0.2, 0.3): 0.05 s of silence, then the media begins at sample 12'000.
    ASSERT_TRUE((*reader)->read(Time::fromSecondsF(0.2), 4800, buf.data()));
    EXPECT_FLOAT_EQ(buf[0], 0.0f);
    EXPECT_FLOAT_EQ(buf[2 * 2399], 0.0f);
    for (int i = 2400; i < 4800; ++i) {
        const float want = test::rampSample(9600 + i);
        ASSERT_NEAR(buf[2 * i], want, 2e-5f) << i;
        ASSERT_FLOAT_EQ(buf[2 * i], buf[2 * i + 1]);  // mono on both channels, full level
    }
    // After the end: silence.
    ASSERT_TRUE((*reader)->read(Time::fromSecondsF(10.0), 4800, buf.data()));
    for (float v : buf) ASSERT_EQ(v, 0.0f);
}

TEST(AudioReader, SequentialChunksAreGaplessAndBackwardJumpsSeek) {
    test::TempDir dir;
    const auto path = dir / "mic.mkv";
    ASSERT_TRUE(test::writeTestAudio(path, {.seconds = 3.0, .channels = 2}));
    auto reader = AudioReader::open(path);
    ASSERT_TRUE(reader);
    std::vector<float> buf(1000 * 2);
    std::int64_t pos = 24'000;  // start mid-file
    for (int chunk = 0; chunk < 90; ++chunk, pos += 1000) {
        ASSERT_TRUE((*reader)->read(Time::fromSamples(pos, 48'000), 1000, buf.data()));
        for (int i = 0; i < 1000; ++i) ASSERT_NEAR(buf[2 * i], test::rampSample(pos + i), 2e-5f) << pos + i;
    }
    EXPECT_EQ((*reader)->seeks(), 1u);
    ASSERT_TRUE((*reader)->read(Time::fromSamples(4321, 48'000), 1000, buf.data()));
    EXPECT_NEAR(buf[0], test::rampSample(4321), 2e-5f);
    EXPECT_EQ((*reader)->seeks(), 2u);
}

TEST(AudioReader, ResamplesOtherRates) {
    test::TempDir dir;
    const auto path = dir / "music.mkv";
    ASSERT_TRUE(test::writeTestAudio(path, {.seconds = 2.0, .sampleRate = 44'100, .channels = 2}));
    auto reader = AudioReader::open(path);
    ASSERT_TRUE(reader);
    std::vector<float> buf(48'000 * 2);
    ASSERT_TRUE((*reader)->read(Time::fromSecondsF(0.5), 48'000, buf.data()));
    // Mid-ramp, the 48 kHz signal follows the 44.1 kHz ramp at the same media time.
    for (int i : {1000, 20'000, 40'000}) {
        const double t = 0.5 + i / 48'000.0;
        const auto n = static_cast<std::int64_t>(std::llround(t * 44'100));
        if (n % 1000 < 20 || n % 1000 > 980) continue;  // skip the sawtooth's jump
        EXPECT_NEAR(buf[2 * i], test::rampSample(n), 0.01f) << i;
    }
}

TEST(Waveform, PeaksFollowLoudnessOverTime) {
    test::TempDir dir;
    const auto path = dir / "speech.mkv";
    // Loud for 1 s, silent for 1 s, quiet (−32 dB) for 1 s.
    ASSERT_TRUE(test::writeTestAudio(path, {.seconds = 3.0, .channels = 1, .startSeconds = 0.5, .envelope = [](double t) {
                                                return t < 1.5 ? 1.0f : t < 2.5 ? 0.0f : 0.05f;
                                            }}));
    auto w = computeWaveform(path, 100);
    ASSERT_TRUE(w) << w.error().toString();
    EXPECT_NEAR(w->start.toSecondsF(), 0.5, 1e-6);
    EXPECT_NEAR(w->duration().toSecondsF(), 3.0, 0.02);
    const auto at = [&](double s) { return w->maxIn(Time::fromSecondsF(s), Time::fromSecondsF(s + 0.2)); };
    EXPECT_NEAR(at(0.8), 221, 2);  // ramp peaks at 0.4 = −7.96 dBFS → (52.04 / 60) × 255
    EXPECT_EQ(at(2.0), 0);    // digital silence
    EXPECT_GT(at(3.0), 60);   // quiet speech stays visible
    EXPECT_LT(at(3.0), 140);
    EXPECT_EQ(at(9.0), 0);    // after the end
    EXPECT_EQ(peakToByte(1.0f), 255);
    EXPECT_EQ(peakToByte(0.0f), 0);
    EXPECT_EQ(peakToByte(0.001f), 0);  // −60 dB
}
