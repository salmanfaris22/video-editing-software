#include "editor/AudioMixer.h"
#include "editor/EditorFixture.h"
#include "editor/SilenceDetector.h"
#include "timeline/EditOps.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace lectern;
using namespace lectern::editor;

namespace {

Time sec(double s) { return Time::fromSecondsF(s); }

/// Mixes `frames` at timeline second `t` and returns the left channel.
std::vector<float> mixLeft(const project::Project& p, const std::filesystem::path& dir, double t, int frames) {
    AudioMixer mixer(dir);
    mixer.setProject(std::make_shared<const project::Project>(p));
    std::vector<float> out(static_cast<std::size_t>(frames) * 2);
    EXPECT_TRUE(mixer.mix(Time::fromSecondsF(t), frames, out.data()));
    std::vector<float> left(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        left[static_cast<std::size_t>(i)] = out[static_cast<std::size_t>(2 * i)];
        EXPECT_EQ(out[static_cast<std::size_t>(2 * i)], out[static_cast<std::size_t>(2 * i + 1)]);
    }
    return left;
}

}  // namespace

TEST(AudioMixer, PlaysTheTimelineSampleExactly) {
    test::EditorFixture f;
    AudioMixer mixer(f.dir.path());
    mixer.setProject(f.snapshot());
    std::vector<float> out(1024 * 2);
    for (std::int64_t block = 0; block < 20; ++block) {  // sequential blocks, gapless
        const std::int64_t at = 48'000 + block * 1024;
        ASSERT_TRUE(mixer.mix(at, 1024, out.data()));
        for (int i = 0; i < 1024; ++i) ASSERT_NEAR(out[2 * i], test::rampSample(at + i), 2e-5f) << at + i;
    }
    EXPECT_GT(mixer.peakLeft(), 0.3f);
}

TEST(AudioMixer, FollowsRippleCuts) {
    test::EditorFixture f;
    ASSERT_TRUE(timeline::edit::removeRange(f.project.timeline, {sec(1.0), sec(0.5)}));
    const auto left = mixLeft(f.project, f.dir.path(), 1.0, 512);
    for (int i = 0; i < 512; ++i) ASSERT_NEAR(left[i], test::rampSample(72'000 + i), 2e-5f);  // source 1.5 s
}

TEST(AudioMixer, TrackAndClipGainMuteAndSolo) {
    test::EditorFixture f;
    f.track("Microphone").gainDb = -6.0206;  // × 0.5
    auto half = mixLeft(f.project, f.dir.path(), 0.5, 256);
    for (int i = 0; i < 256; ++i) ASSERT_NEAR(half[i], 0.5f * test::rampSample(24'000 + i), 1e-4f);
    f.track("Microphone").clips[0].audio.gainDb = 6.0206;  // cancels out
    auto unity = mixLeft(f.project, f.dir.path(), 0.5, 256);
    EXPECT_NEAR(unity[100], test::rampSample(24'100), 1e-4f);

    f.track("Microphone").clips[0].audio.muted = true;
    for (float v : mixLeft(f.project, f.dir.path(), 0.5, 256)) ASSERT_EQ(v, 0.0f);
    f.track("Microphone").clips[0].audio.muted = false;
    f.track("Microphone").muted = true;
    for (float v : mixLeft(f.project, f.dir.path(), 0.5, 256)) ASSERT_EQ(v, 0.0f);
    f.track("Microphone").muted = false;

    f.addAudioTrack("Music", {.seconds = 3.0, .channels = 2, .gain = 0.25f});
    auto both = mixLeft(f.project, f.dir.path(), 0.5, 64);
    EXPECT_NEAR(both[10], test::rampSample(24'010) * 1.25f, 1e-4f);  // mic (net unity) + music (0.25)
    f.track("Music").solo = true;
    auto solo = mixLeft(f.project, f.dir.path(), 0.5, 64);
    EXPECT_NEAR(solo[10], test::rampSample(24'010) * 0.25f, 1e-4f);
}

TEST(AudioMixer, FadesAndVolumeAutomation) {
    test::EditorFixture f;
    auto& clip = f.track("Microphone").clips[0];
    clip.audio.fadeIn = sec(1);
    clip.audio.fadeOut = sec(1);
    auto in = mixLeft(f.project, f.dir.path(), 0.5, 16);
    EXPECT_NEAR(in[0], 0.5f * test::rampSample(24'000), 2e-3f);
    auto out = mixLeft(f.project, f.dir.path(), 2.75, 16);
    EXPECT_NEAR(out[0], 0.25f * test::rampSample(132'000), 2e-3f);

    clip.audio.fadeIn = clip.audio.fadeOut = Time::zero();
    clip.audio.volume.setKey(sec(1), 1.0);
    clip.audio.volume.setKey(sec(2), 0.0);
    auto ramp = mixLeft(f.project, f.dir.path(), 1.5, 16);
    EXPECT_NEAR(ramp[0], 0.5f * test::rampSample(72'000), 2e-3f);
}

TEST(AudioMixer, ClampsAndReportsMissingMedia) {
    test::EditorFixture f;
    f.track("Microphone").gainDb = 24;
    for (float v : mixLeft(f.project, f.dir.path(), 0.5, 2048)) ASSERT_LE(std::fabs(v), 1.0f);
    f.track("Microphone").gainDb = 0;
    std::filesystem::remove(f.dir / "mic.mkv");
    AudioMixer mixer(f.dir.path());
    mixer.setProject(f.snapshot());
    std::vector<float> out(512 * 2, 1.0f);
    ASSERT_TRUE(mixer.mix(sec(0.5), 512, out.data()));
    for (float v : out) ASSERT_EQ(v, 0.0f);
    ASSERT_EQ(mixer.missingMedia().size(), 1u);
    EXPECT_EQ(mixer.missingMedia()[0], "Microphone");
}

TEST(SilenceDetector, FindsLongPausesInTheVoiceAndIgnoresMusic) {
    // Speech 0–1 s, pause 1–2.5 s, speech 2.5–3.5 s, short breath 3.5–3.9 s, speech to 5 s.
    auto envelope = [](double t) { return (t >= 1.0 && t < 2.5) || (t >= 3.5 && t < 3.9) ? 0.0f : 1.0f; };
    test::EditorFixture f({.seconds = 5.0, .micEnvelope = envelope});
    f.addAudioTrack("Music", {.seconds = 5.0, .channels = 2, .gain = 0.5f});
    std::vector<double> progress;
    auto silences = detectSilences(f.project, f.dir.path(), {}, [&](double p) { progress.push_back(p); });
    ASSERT_TRUE(silences) << silences.error().toString();
    ASSERT_EQ(silences->size(), 1u);
    EXPECT_NEAR((*silences)[0].start.toSecondsF(), 1.15, 0.03);
    EXPECT_NEAR((*silences)[0].end().toSecondsF(), 2.35, 0.03);
    ASSERT_FALSE(progress.empty());
    EXPECT_DOUBLE_EQ(progress.back(), 1.0);

    auto removed = timeline::edit::removeRanges(f.project.timeline, *silences);
    ASSERT_TRUE(removed);
    EXPECT_NEAR(f.project.timeline.duration().toSecondsF(), 5.0 - removed->toSecondsF(), 1e-9);
    EXPECT_TRUE(f.project.validate());

    std::atomic<bool> cancel{true};
    auto cancelled = detectSilences(f.project, f.dir.path(), {}, {}, &cancel);
    ASSERT_FALSE(cancelled);
    EXPECT_EQ(cancelled.error().code(), ErrorCode::Cancelled);
}
