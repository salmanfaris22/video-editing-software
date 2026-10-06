#include "capture/DiskSpaceMonitor.h"
#include "capture/MuxWorker.h"
#include "media/VideoEncoder.h"
#include "capture/EncodingPresets.h"
#include "capture/SessionManifest.h"
#include "support/TestSupport.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <thread>

using namespace lectern;
using namespace lectern::capture;

TEST(EncodingPresets, FitWithinPreservesAspectAndEvenSizes) {
    EXPECT_EQ(fitWithin({2880, 1800}, {1920, 1080}), (Resolution{1728, 1080}));
    EXPECT_EQ(fitWithin({1280, 720}, {1920, 1080}), (Resolution{1280, 720}));  // never upscale
    EXPECT_EQ(fitWithin({1001, 777}, {0, 0}), (Resolution{1002, 778}));
    EXPECT_EQ(resolutionPreset("4k"), (Resolution{3840, 2160}));
    EXPECT_FALSE(resolutionPreset("native"));
}

TEST(EncodingPresets, LetterboxCentersContentOnEvenCoordinates) {
    EXPECT_EQ(letterbox({1920, 1080}, {1920, 1080}), (PixelRect{0, 0, 1920, 1080}));
    EXPECT_EQ(letterbox({1000, 1000}, {1920, 1080}), (PixelRect{420, 0, 1080, 1080}));  // pillarbox
    EXPECT_EQ(letterbox({1920, 800}, {1920, 1080}), (PixelRect{0, 140, 1920, 800}));    // letterbox
    EXPECT_EQ(letterbox({960, 540}, {1920, 1080}), (PixelRect{0, 0, 1920, 1080}));      // scales up
    const PixelRect odd = letterbox({1366, 767}, {1281, 721});
    EXPECT_EQ(odd.x % 2, 0);
    EXPECT_EQ(odd.y % 2, 0);
    EXPECT_EQ(odd.width % 2, 0);
    EXPECT_EQ(odd.height % 2, 0);
    EXPECT_LE(odd.x + odd.width, 1281);
    EXPECT_LE(odd.y + odd.height, 721);
    EXPECT_EQ(letterbox({0, 10}, {1920, 1080}), PixelRect{});
}

TEST(EncodingPresets, BitratesScaleWithResolutionFpsAndQuality) {
    const auto b1080 = videoBitrate(TrackRole::Screen, {1920, 1080}, FrameRate(30, 1), QualityPreset::High);
    const auto b1080p60 = videoBitrate(TrackRole::Screen, {1920, 1080}, FrameRate(60, 1), QualityPreset::High);
    const auto b4k = videoBitrate(TrackRole::Screen, {3840, 2160}, FrameRate(30, 1), QualityPreset::High);
    EXPECT_EQ(b1080, 10'000'000);
    EXPECT_EQ(b1080p60, 16'000'000);
    EXPECT_EQ(b4k, 35'000'000);
    EXPECT_LT(videoBitrate(TrackRole::Screen, {1920, 1080}, FrameRate(30, 1), QualityPreset::Standard), b1080);
    EXPECT_GT(videoBitrate(TrackRole::Camera, {1920, 1080}, FrameRate(30, 1), QualityPreset::High), b1080);
}

TEST(DiskSpaceMonitor, WarnsAndGoesCriticalFromRateAndFreeSpace) {
    std::uint64_t available = 50ull << 30;
    DiskSpaceMonitor mon(DiskSpacePolicy{}, [&](const std::filesystem::path&) -> Result<fs::DiskSpace> {
        return fs::DiskSpace{100ull << 30, available, available};
    });
    constexpr std::uint64_t kRate = 5ull << 20;  // 5 MB/s ≈ 1080p60 screen + camera + audio
    EXPECT_EQ(mon.update("/x", 0, 0).level, DiskLevel::Ok);
    auto s = mon.update("/x", kRate, 1'000'000'000);
    EXPECT_EQ(s.level, DiskLevel::Ok);
    EXPECT_NEAR(s.bytesPerSecond, static_cast<double>(kRate), 1.0);
    EXPECT_NEAR(s.secondsRemaining, 10240, 5);

    available = 3ull << 30;  // < 5 GB free (≈ 10 min left) → warning
    EXPECT_EQ(mon.update("/x", 2 * kRate, 2'000'000'000).level, DiskLevel::Warning);

    available = 512ull << 20;  // < 1 GB free → critical
    EXPECT_EQ(mon.update("/x", 3 * kRate, 3'000'000'000).level, DiskLevel::Critical);

    // Plenty of space but a very high write rate: critical by time remaining.
    available = 6ull << 30;
    DiskSpaceMonitor fast(DiskSpacePolicy{}, [&](const std::filesystem::path&) -> Result<fs::DiskSpace> {
        return fs::DiskSpace{100ull << 30, available, available};
    });
    (void)fast.update("/x", 0, 0);
    EXPECT_EQ(fast.update("/x", 100ull << 20, 1'000'000'000).level, DiskLevel::Critical);  // 100 MB/s → 61 s
}

TEST(SessionManifest, RoundTripsThroughDisk) {
    test::TempDir dir;
    SessionManifest m;
    m.sessionId = "0199b2a4-6f3e-7c41-9a5e-2b0d7c9e1f00";
    m.title = "Lesson";
    m.state = SessionState::Recording;
    m.hostClock = hostClockName();
    m.startHostNs = 123;
    m.stopHostNs = 456;
    m.duration = Time::fromSeconds(42);
    m.pauses.push_back({200, 300, Time::fromSeconds(10)});
    ManifestTrack v;
    v.id = "screen";
    v.role = TrackRole::Screen;
    v.mediaType = MediaType::Video;
    v.file = "media/screen/screen-1.mkv";
    v.width = 1920;
    v.height = 1080;
    v.frameRate = FrameRate(60, 1);
    v.state = TrackState::Completed;
    v.start = Time::zero();
    v.end = Time::fromSeconds(42);
    ManifestTrack a;
    a.id = "microphone";
    a.role = TrackRole::Microphone;
    a.mediaType = MediaType::Audio;
    a.file = "media/audio/microphone-1.mkv";
    a.sampleRate = 48'000;
    a.channels = 1;
    a.driftPpm = 12.5;
    a.state = TrackState::Recording;
    m.tracks = {v, a};

    ASSERT_TRUE(writeManifest(dir / "session.json", m));
    auto loaded = readManifest(dir / "session.json");
    ASSERT_TRUE(loaded) << loaded.error().toString();
    EXPECT_EQ(loaded->sessionId, m.sessionId);
    EXPECT_EQ(loaded->duration, m.duration);
    ASSERT_EQ(loaded->tracks.size(), 2u);
    EXPECT_EQ(loaded->tracks[0].frameRate, FrameRate(60, 1));
    EXPECT_EQ(loaded->tracks[1].state, TrackState::Recording);
    EXPECT_DOUBLE_EQ(loaded->tracks[1].driftPpm, 12.5);
    ASSERT_EQ(loaded->pauses.size(), 1u);
    EXPECT_EQ(*loaded->pauses[0].hostEndNs, 300);
}

TEST(MuxWorker, DiskStallDropsWholeGopsSoTheFileStaysDecodable) {
    test::TempDir dir;
    media::VideoEncoderConfig cfg;
    cfg.width = 160;
    cfg.height = 90;
    cfg.frameRate = FrameRate(30, 1);
    cfg.gopFrames = 10;
    cfg.bitRate = 400'000;
    cfg.preference = media::EncoderPreference::SoftwareOnly;
    cfg.realtime = true;
    auto enc = media::VideoEncoder::create(cfg);
    ASSERT_TRUE(enc) << enc.error().toString();

    // Simulated disk stall: the first write blocks until the encoder side has
    // been forced to drop a packet (queue full past pushTimeout); then the
    // "disk" recovers. Deterministic regardless of encoder latency.
    std::atomic<MuxWorker*> worker{nullptr};
    bool stalled = false;
    MuxWorker::Options opts;
    opts.queueBytes = 1;  // one packet at a time
    opts.pushTimeout = std::chrono::milliseconds(20);
    opts.beforeWrite = [&] {
        if (stalled) return;
        stalled = true;
        for (int i = 0; i < 1000; ++i) {
            MuxWorker* w = worker.load();
            if (w && w->packetsDropped() > 0) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    MuxWorker mux("stall", dir / "stall.mkv", media::recordingMuxerOptions(), opts);
    worker = &mux;
    ASSERT_TRUE(mux.open());
    ASSERT_TRUE(mux.configure((*enc)->context()));

    int accepted = 0;
    const AVRational tb = (*enc)->timeBase();
    auto sink = [&](media::Packet&& p) -> Status {
        if (mux.push(std::move(p), tb)) ++accepted;
        return ok();
    };
    for (int i = 0; i < 40; ++i) {
        auto frame = media::Frame::allocVideo(160, 90, AV_PIX_FMT_NV12);
        ASSERT_TRUE(frame);
        for (int y = 0; y < 90; ++y) std::memset((*frame)->data[0] + y * (*frame)->linesize[0], (i * 9 + y) & 0xFF, 160);
        for (int y = 0; y < 45; ++y) std::memset((*frame)->data[1] + y * (*frame)->linesize[1], 128, 160);
        (*frame)->pts = i;
        ASSERT_TRUE((*enc)->encode(std::move(*frame), i % 10 == 0, sink));
    }
    ASSERT_TRUE((*enc)->flush(sink));
    mux.finish();
    ASSERT_TRUE(mux.waitFinished(std::chrono::seconds(10)));
    EXPECT_GT(mux.packetsDropped(), 0u);

    // Everything that was written decodes; dropping resumed only at a keyframe.
    auto decoded = test::decodeVideo(dir / "stall.mkv");
    ASSERT_TRUE(decoded) << decoded.error().toString();
    EXPECT_EQ(static_cast<int>(decoded->ptsSeconds.size()), accepted);
    EXPECT_GE(accepted, 20);  // GOPs after the stall are complete
}
