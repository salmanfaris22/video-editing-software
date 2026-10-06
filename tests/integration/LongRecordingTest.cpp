// Long-run tests (label "long", excluded from the default test presets):
//   ctest --test-dir build/dev -L long --output-on-failure
//
// One simulated hour of recording in fast-forward (manual clock), with a
// drifting audio device clock. Verifies that duration and sample counts are
// exact, that audio and video are still in sync at the end of the hour, and
// that memory does not grow with recording length (docs/PERFORMANCE.md §5).

#include "integration/SessionHarness.h"

#include <cmath>
#include <cstdio>

using namespace lectern;
using namespace lectern::capture;
using namespace lectern::test::session;

namespace {
double nearest(const std::vector<double>& values, double target) {
    double best = 1e9;
    for (double v : values) best = std::min(best, std::fabs(v - target));
    return best;
}
}  // namespace

TEST(LongRecording, OneHourStaysInSyncWithFlatMemory) {
    Harness h;
    SyntheticVideoOptions vo;
    vo.width = 160;
    vo.height = 90;
    vo.frameRate = FrameRate(30, 1);
    vo.jitterMs = 2.0;
    vo.flashPeriodNs = kSec;
    vo.flashDurationNs = 100 * kMs;
    SyntheticAudioOptions ao;
    ao.driftPpm = 250.0;  // uncorrected: 900 ms off after one hour
    ao.jitterMs = 1.0;
    ao.clickPeriodNs = kSec;
    h.setup(vo, ao);

    constexpr std::int64_t kStep = 100 * kMs;
    constexpr int kMinutes = 60;
    h.produce();
    ASSERT_TRUE(h.session->start());
    std::uint64_t rssAt10 = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int minute = 1; minute <= kMinutes; ++minute) {
        h.advanceTo(kOrigin + minute * 60 * kSec, kStep);
        if (minute == 10) rssAt10 = test::currentRssBytes();
    }
    const std::uint64_t rssAt60 = test::currentRssBytes();
    ASSERT_TRUE(h.session->stop());
    h.advanceTo(h.clock.nowNs() + 700 * kMs, kStep);
    const SessionResult r = h.finish();
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    ASSERT_EQ(r.state, SessionState::Completed);
    EXPECT_EQ(r.manifest.duration, Time::fromSeconds(kMinutes * 60));
    const auto mic = std::find_if(r.manifest.tracks.begin(), r.manifest.tracks.end(),
                                  [](const ManifestTrack& t) { return t.id == "microphone"; });
    const auto screen = std::find_if(r.manifest.tracks.begin(), r.manifest.tracks.end(),
                                     [](const ManifestTrack& t) { return t.id == "screen"; });
    ASSERT_NE(mic, r.manifest.tracks.end());
    ASSERT_NE(screen, r.manifest.tracks.end());
    EXPECT_EQ(screen->framesEncoded, static_cast<std::uint64_t>(kMinutes * 60 * 30));
    EXPECT_NEAR(mic->driftPpm, 250.0, 25.0);
    EXPECT_EQ(mic->hardCorrections, 0u);

    // Sync in the last 20 seconds of the hour.
    const double from = kMinutes * 60 - 20.0;
    auto video = test::decodeVideo(h.file(r, "screen"), from, 20.0);
    auto audio = test::decodeAudio(h.file(r, "microphone"), from, 20.0);
    ASSERT_TRUE(video) << video.error().toString();
    ASSERT_TRUE(audio) << audio.error().toString();
    std::vector<bool> flash;
    for (double l : video->meanLuma) flash.push_back(l > 200.0);
    const auto flashes = test::risingEdges(video->ptsSeconds, flash);
    const auto clicks = test::burstStarts(*audio, 0.5f, 0.5);
    double worst = 0;
    int checked = 0;
    for (int k = 1; k < 19; ++k) {
        const double e = from + k;
        const double v = nearest(flashes, e);
        const double a = nearest(clicks, e);
        EXPECT_LT(v, 0.002) << "flash at " << e;
        EXPECT_LT(a, 0.003) << "click at " << e;
        worst = std::max(worst, std::fabs(v - a));
        ++checked;
    }
    EXPECT_EQ(checked, 18);
    EXPECT_LT(worst, 0.005);

    // Audio length is exact to the sample.
    auto tail = test::decodeAudio(h.file(r, "microphone"), kMinutes * 60 - 1.0, 5.0);
    ASSERT_TRUE(tail);
    const double audioEnd = tail->startSeconds + static_cast<double>(tail->channel0.size()) / tail->sampleRate;
    EXPECT_NEAR(audioEnd, kMinutes * 60.0, 0.001);

    // Memory must not grow with recording length.
    const double growthMb = (static_cast<double>(rssAt60) - static_cast<double>(rssAt10)) / (1024.0 * 1024.0);
    EXPECT_LT(growthMb, 24.0);
    std::printf("1 h simulated in %.1f s; A/V worst offset %.3f ms; drift %.1f ppm; RSS 10 min %.1f MB → 60 min %.1f MB\n",
                wall, worst * 1000.0, mic->driftPpm, static_cast<double>(rssAt10) / 1048576.0,
                static_cast<double>(rssAt60) / 1048576.0);
}
