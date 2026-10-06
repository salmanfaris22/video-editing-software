// End-to-end recording tests with deterministic synthetic sources and a
// manual clock (docs/RECORDING_ENGINE.md §13). They exercise the complete
// pipeline: live sources → session clock → pacer/drift control → encoders →
// Matroska files → manifest → project import.

#include "capture/RecordingSession.h"
#include "capture/synthetic/SyntheticSources.h"
#include "media/FFmpeg.h"
#include "media/MediaProbe.h"
#include "project/ProjectStore.h"
#include "services/RecordingImporter.h"
#include "integration/SessionHarness.h"
#include "support/TestSupport.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace lectern;
using namespace lectern::capture;

namespace {

using namespace lectern::test::session;

double nearestDistance(const std::vector<double>& values, double target) {
    double best = 1e9;
    for (double v : values) best = std::min(best, std::fabs(v - target));
    return best;
}

}  // namespace

TEST(RecordingSession, EndToEndSyncWithDriftJitterAndPause) {
    Harness h;
    SyntheticVideoOptions vo;
    vo.width = 320;
    vo.height = 180;
    vo.frameRate = FrameRate(30, 1);
    vo.jitterMs = 3.0;
    vo.flashPeriodNs = kSec;
    vo.flashDurationNs = 100 * kMs;
    SyntheticAudioOptions ao;
    ao.driftPpm = 500.0;  // uncorrected this would be 8.5 ms off after 17 s
    ao.jitterMs = 1.0;
    ao.clickPeriodNs = kSec;
    h.setup(vo, ao);

    h.produce();  // pre-roll frame at T0
    ASSERT_TRUE(h.session->start());
    h.advanceTo(kOrigin + 8500 * kMs);
    ASSERT_TRUE(h.session->pause());
    h.advanceTo(kOrigin + 11'500 * kMs);  // 3 s paused (sources keep running)
    ASSERT_TRUE(h.session->resume());
    h.advanceTo(kOrigin + 20 * kSec);
    const RecordingStats live = h.session->stats();
    EXPECT_EQ(live.state, SessionState::Recording);
    ASSERT_TRUE(h.session->stop());
    h.advanceTo(kOrigin + 20'600 * kMs);  // post-stop data lets writers drain

    const SessionResult r = h.finish();
    ASSERT_EQ(r.state, SessionState::Completed);
    EXPECT_EQ(r.manifest.duration, Time::fromSeconds(17));
    ASSERT_EQ(r.manifest.pauses.size(), 1u);
    EXPECT_EQ(r.manifest.pauses[0].sessionTime, Time::fromMilliseconds(8500));
    for (const auto& t : r.manifest.tracks) {
        EXPECT_EQ(t.state, TrackState::Completed) << t.id << ": " << t.error;
    }

    // ---- video: exact CFR frame count, keyframe at the resume point
    auto video = test::decodeVideo(h.file(r, "screen"));
    ASSERT_TRUE(video) << video.error().toString();
    EXPECT_EQ(video->ptsSeconds.size(), 17u * 30u);
    for (std::size_t i = 0; i < video->ptsSeconds.size(); ++i) {
        if (std::fabs(video->ptsSeconds[i] - 8.5) < 0.001) EXPECT_TRUE(video->keyframes[i]) << "no keyframe at resume";
    }

    // ---- audio: exact length, measured drift
    auto audio = test::decodeAudio(h.file(r, "microphone"));
    ASSERT_TRUE(audio) << audio.error().toString();
    EXPECT_NEAR(audio->startSeconds, 0.0, 0.0011);
    EXPECT_NEAR(audio->startSeconds + static_cast<double>(audio->channel0.size()) / audio->sampleRate, 17.0, 0.002);
    const auto micTrack = std::find_if(r.manifest.tracks.begin(), r.manifest.tracks.end(),
                                       [](const ManifestTrack& t) { return t.id == "microphone"; });
    ASSERT_NE(micTrack, r.manifest.tracks.end());
    EXPECT_NEAR(micTrack->driftPpm, 500.0, 40.0);

    // ---- sync: every flash/click pair lines up on the session timeline
    std::vector<bool> flash;
    for (double l : video->meanLuma) flash.push_back(l > 200.0);
    const auto flashes = test::risingEdges(video->ptsSeconds, flash);
    const auto clicks = test::burstStarts(*audio, 0.5f, 0.5);
    std::vector<double> expected;
    for (int k = 1; k <= 16; ++k) expected.push_back(k);  // hosts 1..8 → 1..8, hosts 12..19 → 9..16
    double worstAv = 0;
    for (double e : expected) {
        const double v = nearestDistance(flashes, e);
        const double a = nearestDistance(clicks, e);
        EXPECT_LT(v, 0.002) << "flash at " << e;
        EXPECT_LT(a, 0.003) << "click at " << e;
        worstAv = std::max(worstAv, std::fabs(v - a));
    }
    EXPECT_LT(worstAv, 0.005);
    std::printf("A/V sync over 17 s with 500 ppm drift: worst offset %.3f ms\n", worstAv * 1000.0);
}

TEST(RecordingSession, LateStartingTrackKeepsItsOffset) {
    Harness h;
    h.audioStartNs = kOrigin + 400 * kMs;  // microphone delivers its first audio 400 ms after T0
    SyntheticVideoOptions vo;
    vo.width = 160;
    vo.height = 90;
    h.setup(vo, SyntheticAudioOptions{});
    h.produce();
    ASSERT_TRUE(h.session->start());
    h.advanceTo(kOrigin + 3 * kSec);
    ASSERT_TRUE(h.session->stop());
    h.advanceTo(kOrigin + 3600 * kMs);
    const SessionResult r = h.finish();
    auto audio = test::decodeAudio(h.file(r, "microphone"));
    ASSERT_TRUE(audio);
    EXPECT_NEAR(audio->startSeconds, 0.4, 0.002);
    EXPECT_NEAR(audio->startSeconds + static_cast<double>(audio->channel0.size()) / audio->sampleRate, 3.0, 0.002);
}

TEST(RecordingSession, MicrophoneGapIsFilledAndSyncIsKept) {
    Harness h;
    SyntheticVideoOptions vo;
    vo.width = 160;
    vo.height = 90;
    SyntheticAudioOptions ao;
    ao.clickPeriodNs = kSec;
    ao.gaps = {{kOrigin + 2200 * kMs, kOrigin + 2700 * kMs}};  // device delivers nothing for 0.5 s
    h.setup(vo, ao);
    h.produce();
    ASSERT_TRUE(h.session->start());
    h.advanceTo(kOrigin + 5 * kSec);
    ASSERT_TRUE(h.session->stop());
    h.advanceTo(kOrigin + 5600 * kMs);
    const SessionResult r = h.finish();
    auto audio = test::decodeAudio(h.file(r, "microphone"));
    ASSERT_TRUE(audio);
    EXPECT_NEAR(static_cast<double>(audio->channel0.size()) / audio->sampleRate, 5.0, 0.002);
    const auto clicks = test::burstStarts(*audio, 0.5f, 0.5);
    for (double e : {1.0, 2.0, 3.0, 4.0}) EXPECT_LT(nearestDistance(clicks, e), 0.003) << "click at " << e;
    const auto mic = std::find_if(r.manifest.tracks.begin(), r.manifest.tracks.end(),
                                  [](const ManifestTrack& t) { return t.id == "microphone"; });
    EXPECT_GE(mic->hardCorrections, 1u);
}

TEST(RecordingSession, DiskFullStopsGracefullyWithValidFiles) {
    Harness h;
    std::atomic<std::uint64_t> available{8ull << 30};
    SyntheticVideoOptions vo;
    vo.width = 160;
    vo.height = 90;
    h.setup(vo, SyntheticAudioOptions{}, [&](const std::filesystem::path&) -> Result<fs::DiskSpace> {
        return fs::DiskSpace{16ull << 30, available.load(), available.load()};
    });
    h.produce();
    ASSERT_TRUE(h.session->start());
    h.advanceTo(kOrigin + 1 * kSec);
    available = 200ull << 20;  // the disk fills up
    // The monitor polls every 2 s of real time; keep producing until it reacts.
    for (int i = 0; i < 400 && h.session->state() == SessionState::Recording; ++i) {
        h.advanceTo(h.clock.nowNs() + 10 * kMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    h.advanceTo(h.clock.nowNs() + 600 * kMs);
    const SessionResult r = h.finish();
    EXPECT_EQ(r.reason, StopReason::DiskFull);
    EXPECT_EQ(r.state, SessionState::Completed);
    EXPECT_TRUE(media::probeMedia(h.file(r, "screen")));
    EXPECT_TRUE(media::probeMedia(h.file(r, "microphone")));
}

TEST(RecordingSession, CancelDeletesEverything) {
    Harness h;
    SyntheticVideoOptions vo;
    vo.width = 160;
    vo.height = 90;
    h.setup(vo, SyntheticAudioOptions{});
    h.produce();
    ASSERT_TRUE(h.session->start());
    h.advanceTo(kOrigin + 1 * kSec);
    const auto screenFile = h.session->projectDir() / "media" / "screen";
    h.session->cancel();
    EXPECT_EQ(h.session->state(), SessionState::Cancelled);
    EXPECT_FALSE(std::filesystem::exists(h.session->sessionDir()));
    bool anyMedia = false;
    for (const auto& de : std::filesystem::directory_iterator(screenFile)) anyMedia |= de.is_regular_file();
    EXPECT_FALSE(anyMedia);
    EXPECT_TRUE(std::filesystem::is_empty(h.dir / "registry"));
}

TEST(RecordingImporter, BuildsSyncedProjectFromSession) {
    Harness h;
    SyntheticVideoOptions vo;
    vo.width = 320;
    vo.height = 180;
    h.audioStartNs = kOrigin + 250 * kMs;
    h.setup(vo, SyntheticAudioOptions{});
    h.produce();
    ASSERT_TRUE(h.session->start());
    h.advanceTo(kOrigin + 2 * kSec);
    ASSERT_TRUE(h.session->pause());
    h.advanceTo(kOrigin + 3 * kSec);
    ASSERT_TRUE(h.session->resume());
    h.advanceTo(kOrigin + 4 * kSec);
    ASSERT_TRUE(h.session->stop());
    h.advanceTo(kOrigin + 4600 * kMs);
    const SessionResult r = h.finish();

    auto proj = services::importRecording(r.projectDir, r.manifest);
    ASSERT_TRUE(proj) << proj.error().toString();
    ASSERT_EQ(proj->timeline.tracks.size(), 2u);
    EXPECT_EQ(proj->timeline.tracks[0].kind, timeline::TrackKind::Video);
    EXPECT_EQ(proj->timeline.tracks[1].kind, timeline::TrackKind::Audio);
    const auto& screenClip = proj->timeline.tracks[0].clips.at(0);
    const auto& micClip = proj->timeline.tracks[1].clips.at(0);
    EXPECT_EQ(screenClip.range.start, Time::zero());
    EXPECT_NEAR(micClip.range.start.toSecondsF(), 0.25, 0.002);  // offset preserved
    EXPECT_NEAR(micClip.range.end().toSecondsF(), 3.0, 0.002);
    EXPECT_EQ(screenClip.linkGroup, micClip.linkGroup);
    ASSERT_EQ(proj->timeline.markers.size(), 1u);
    EXPECT_EQ(proj->timeline.markers[0].kind, timeline::MarkerKind::Pause);
    EXPECT_EQ(proj->timeline.markers[0].time, Time::fromSeconds(2));
    EXPECT_EQ(proj->canvas.width, 1280);  // 180p screen → smallest canvas preset

    auto loaded = project::ProjectStore::load(r.projectDir);
    ASSERT_TRUE(loaded) << loaded.error().toString();
    EXPECT_EQ(loaded->project, *proj);
}
