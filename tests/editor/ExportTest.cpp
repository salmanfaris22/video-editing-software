#include "editor/EditorFixture.h"
#include "editor/Exporter.h"
#include "media/AudioReader.h"
#include "media/MediaProbe.h"
#include "media/VideoReader.h"
#include "timeline/EditOps.h"

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <iterator>

using namespace lectern;
using namespace lectern::editor;

namespace {

Time sec(double s) { return Time::fromSecondsF(s); }

/// Byte offset of a top-level MP4 atom, or -1.
std::int64_t atomOffset(const std::filesystem::path& path, const char* type) {
    std::ifstream in(path, std::ios::binary);
    std::int64_t offset = 0;
    unsigned char header[16];
    while (in.read(reinterpret_cast<char*>(header), 8)) {
        std::uint64_t size = (std::uint64_t{header[0]} << 24) | (header[1] << 16) | (header[2] << 8) | header[3];
        if (std::string(reinterpret_cast<char*>(header) + 4, 4) == type) return offset;
        if (size == 1) {
            in.read(reinterpret_cast<char*>(header) + 8, 8);
            size = 0;
            for (int i = 8; i < 16; ++i) size = (size << 8) | header[i];
        }
        if (size < 8) return -1;
        offset += static_cast<std::int64_t>(size);
        in.seekg(offset);
    }
    return -1;
}

int centerLuma(const media::Frame& f) {
    const AVFrame* av = f.get();
    return av->data[0][static_cast<std::ptrdiff_t>(av->height / 2) * av->linesize[0] + av->width / 2];
}

}  // namespace

TEST(Export, RendersTheEditedTimelineToAPlayableMp4) {
    test::EditorFixture f;
    f.addText("Exported title", 0.2, 1.0, "lower-third");  // keeps the frame center showing the screen
    ASSERT_TRUE(timeline::edit::removeRange(f.project.timeline, {sec(1.0), sec(0.5)}));  // cut 1.0–1.5 s
    const auto out = f.dir / "Exports" / "lesson.mp4";
    std::vector<double> fractions;
    auto result = exportProject(f.project, f.dir.path(),
                                {.output = out, .width = 640, .height = 360, .quality = "standard", .hardwareEncoder = false},
                                [&](const ExportProgress& p) { fractions.push_back(p.fraction); });
    ASSERT_TRUE(result) << result.error().toString();
    EXPECT_EQ(result->frames, 75);
    EXPECT_TRUE(std::filesystem::exists(out));
    EXPECT_FALSE(std::filesystem::exists(f.dir / "Exports" / "lesson.mp4.partial"));
    ASSERT_FALSE(fractions.empty());
    EXPECT_DOUBLE_EQ(fractions.back(), 1.0);
    EXPECT_TRUE(std::is_sorted(fractions.begin(), fractions.end()));

    auto info = media::probeMedia(out);
    ASSERT_TRUE(info) << info.error().toString();
    ASSERT_NE(info->video(), nullptr);
    ASSERT_NE(info->audio(), nullptr);
    EXPECT_EQ(info->video()->codec, "h264");
    EXPECT_EQ(info->video()->video->width, 640);
    EXPECT_EQ(info->audio()->codec, "aac");
    EXPECT_EQ(info->audio()->audio->sampleRate, 48'000);
    EXPECT_NEAR(info->duration.toSecondsF(), 2.5, 0.1);
    // "faststart": the index precedes the media data, so players start at once.
    const auto moov = atomOffset(out, "moov");
    const auto mdat = atomOffset(out, "mdat");
    ASSERT_GE(moov, 0);
    ASSERT_GE(mdat, 0);
    EXPECT_LT(moov, mdat);

    // Picture: before the cut we see source t, after it source t + 0.5 s.
    auto video = media::VideoReader::open(out, {.hardwareDecode = false});
    ASSERT_TRUE(video) << video.error().toString();
    for (const auto& [exported, source] : {std::pair{0.5, 0.5}, {1.2, 1.7}, {2.0, 2.5}}) {
        auto frame = (*video)->frameAt(sec(exported));
        ASSERT_TRUE(frame);
        const int expected = test::frameLevel(static_cast<std::int64_t>(std::floor(source * 30 + 1e-6)));
        EXPECT_NEAR(centerLuma(*frame), expected, 6) << "export " << exported << " s";
    }

    // Sound: lossy AAC still follows the source ramp across the cut.
    auto audio = media::AudioReader::open(out);
    ASSERT_TRUE(audio) << audio.error().toString();
    std::vector<float> pcm(4000 * 2);
    ASSERT_TRUE((*audio)->read(sec(1.2), 4000, pcm.data()));
    double err = 0;
    for (int i = 0; i < 4000; ++i) {
        const double d = pcm[static_cast<std::size_t>(2 * i)] - test::rampSample(static_cast<std::int64_t>(1.7 * 48'000) + i);
        err += d * d;
    }
    EXPECT_LT(std::sqrt(err / 4000), 0.05);
}

TEST(Export, CancelLeavesNothingBehind) {
    test::EditorFixture f;
    std::atomic<bool> cancel{false};
    const auto out = f.dir / "cancelled.mp4";
    auto result = exportProject(f.project, f.dir.path(),
                                {.output = out, .width = 640, .height = 360, .hardwareEncoder = false},
                                [&](const ExportProgress&) { cancel = true; }, &cancel);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), ErrorCode::Cancelled);
    EXPECT_FALSE(std::filesystem::exists(out));
    EXPECT_FALSE(std::filesystem::exists(f.dir / "cancelled.mp4.partial"));
}

TEST(Export, VerticalCanvasIsLetterboxedIntoALandscapeFrame) {
    test::EditorFixture f({.seconds = 1.0, .camera = false});
    f.project.canvas.width = 1080;
    f.project.canvas.height = 1920;
    f.project.canvas.backgroundColor = "#FF0000";
    auto result = exportProject(f.project, f.dir.path(),
                                {.output = f.dir / "tall.mp4", .width = 640, .height = 360, .hardwareEncoder = false});
    ASSERT_TRUE(result) << result.error().toString();
    auto video = media::VideoReader::open(f.dir / "tall.mp4", {.hardwareDecode = false});
    ASSERT_TRUE(video);
    auto frame = (*video)->frameAt(sec(0.5));
    ASSERT_TRUE(frame);
    EXPECT_LT((*frame)->data[0][static_cast<std::ptrdiff_t>(180) * (*frame)->linesize[0] + 10], 24);  // black side bar
}

TEST(Export, WritesMatroskaWhenTheOutputIsMkv) {
    test::EditorFixture f({.seconds = 0.5});
    const auto out = f.dir / "clip.mkv";
    auto result = exportProject(f.project, f.dir.path(),
                                {.output = out, .width = 640, .height = 360, .quality = "standard", .container = "mkv",
                                 .hardwareEncoder = false});
    ASSERT_TRUE(result) << result.error().toString();
    auto info = media::probeMedia(out);
    ASSERT_TRUE(info) << info.error().toString();
    EXPECT_EQ(info->video()->codec, "h264");
    EXPECT_EQ(info->audio()->codec, "aac");
}

TEST(Export, UsesTheHardwareEncoderWhenAvailable) {
    test::EditorFixture f({.seconds = 1.0});
    auto result = exportProject(f.project, f.dir.path(), {.output = f.dir / "hw.mp4", .width = 1280, .height = 720});
    ASSERT_TRUE(result) << result.error().toString();
    std::printf("export encoder: %s (hardware=%d), %.2f s for 1 s of 720p\n", result->videoEncoder.c_str(),
                result->hardware, result->seconds);
#if defined(__APPLE__)
    EXPECT_TRUE(result->hardware);
#endif
    auto info = media::probeMedia(f.dir / "hw.mp4");
    ASSERT_TRUE(info);
    EXPECT_NEAR(info->duration.toSecondsF(), 1.0, 0.1);
}
