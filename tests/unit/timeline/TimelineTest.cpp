#include "timeline/Timeline.h"
#include "timeline/TimelineJson.h"

#include <gtest/gtest.h>

using namespace lectern;
using namespace lectern::timeline;

namespace {
Clip mediaClip(Time start, Time duration, MediaId media) {
    Clip c;
    c.id = ClipId::generate();
    c.kind = ClipKind::Media;
    c.range = {start, duration};
    c.media = media;
    return c;
}
}  // namespace

TEST(Animated, ConstantAndKeyframedEvaluation) {
    Animated<double> opacity{1.0};
    EXPECT_DOUBLE_EQ(opacity.evaluate(Time::fromSeconds(5)), 1.0);
    opacity.setKey(Time::fromSeconds(0), 0.0, Interpolation::Linear);
    opacity.setKey(Time::fromSeconds(2), 1.0, Interpolation::Linear);
    EXPECT_DOUBLE_EQ(opacity.evaluate(Time::fromSeconds(-1)), 0.0);
    EXPECT_DOUBLE_EQ(opacity.evaluate(Time::fromSeconds(1)), 0.5);
    EXPECT_DOUBLE_EQ(opacity.evaluate(Time::fromSeconds(3)), 1.0);
    opacity.setKey(Time::fromSeconds(0), 0.0, Interpolation::Hold);  // replace
    EXPECT_EQ(opacity.keys.size(), 2u);
    EXPECT_DOUBLE_EQ(opacity.evaluate(Time::fromSeconds(1)), 0.0);
}

TEST(Animated, EasingCurves) {
    const BezierHandles h;
    EXPECT_NEAR(easedProgress(Interpolation::EaseInOut, h, 0.5), 0.5, 1e-4);
    EXPECT_LT(easedProgress(Interpolation::EaseIn, h, 0.25), 0.25);
    EXPECT_GT(easedProgress(Interpolation::EaseOut, h, 0.25), 0.25);
    EXPECT_DOUBLE_EQ(easedProgress(Interpolation::EaseInOut, h, 0.0), 0.0);
    EXPECT_NEAR(easedProgress(Interpolation::EaseInOut, h, 1.0), 1.0, 1e-6);
    // Custom Bézier equal to linear.
    EXPECT_NEAR(easedProgress(Interpolation::Bezier, {0.25, 0.25, 0.75, 0.75}, 0.3), 0.3, 1e-4);
}

TEST(Animated, Vec2Animation) {
    Animated<Vec2> pos{Vec2{0.84, 0.8}};
    pos.setKey(Time::zero(), {0.84, 0.8}, Interpolation::EaseInOut);
    pos.setKey(Time::fromMilliseconds(400), {0.5, 0.5}, Interpolation::Linear);
    const Vec2 mid = pos.evaluate(Time::fromMilliseconds(200));
    EXPECT_NEAR(mid.x, 0.67, 1e-3);
    EXPECT_NEAR(mid.y, 0.65, 1e-3);
}

TEST(Track, ClipAtAndInsertRejectsOverlap) {
    Track t;
    const MediaId m = MediaId::generate();
    ASSERT_TRUE(t.insertClip(mediaClip(Time::fromSeconds(0), Time::fromSeconds(10), m)));
    ASSERT_TRUE(t.insertClip(mediaClip(Time::fromSeconds(20), Time::fromSeconds(5), m)));
    ASSERT_TRUE(t.insertClip(mediaClip(Time::fromSeconds(10), Time::fromSeconds(10), m)));  // fills the gap exactly
    EXPECT_FALSE(t.insertClip(mediaClip(Time::fromSeconds(24), Time::fromSeconds(2), m)));
    EXPECT_FALSE(t.insertClip(mediaClip(Time::fromSeconds(5), Time::fromSeconds(1), m)));
    ASSERT_EQ(t.clips.size(), 3u);
    EXPECT_EQ(t.clipAt(Time::fromSeconds(10)), &t.clips[1]);
    EXPECT_EQ(t.clipAt(Time::fromSeconds(25)), nullptr);
    EXPECT_EQ(t.end(), Time::fromSeconds(25));
}

TEST(Clip, SourceTimeMappingWithSpeed) {
    Clip c = mediaClip(Time::fromSeconds(10), Time::fromSeconds(4), MediaId::generate());
    c.sourceIn = Time::fromSeconds(100);
    c.speed = Rational(2, 1);
    EXPECT_EQ(c.sourceTimeAt(Time::fromSeconds(11)), Time::fromSeconds(102));
    EXPECT_EQ(c.sourceDuration(), Time::fromSeconds(8));
    c.speed = Rational(1, 2);
    EXPECT_EQ(c.sourceTimeAt(Time::fromSeconds(12)), Time::fromSeconds(101));
}

TEST(Timeline, ValidateCatchesBrokenInvariants) {
    Timeline tl;
    const MediaId m = MediaId::generate();
    Track v;
    v.id = TrackId::generate();
    v.clips.push_back(mediaClip(Time::fromSeconds(0), Time::fromSeconds(5), m));
    v.clips.push_back(mediaClip(Time::fromSeconds(4), Time::fromSeconds(5), m));  // overlap
    tl.tracks.push_back(v);
    EXPECT_FALSE(tl.validate());
    tl.tracks[0].clips[1].range.start = Time::fromSeconds(5);
    EXPECT_TRUE(tl.validate([&](const MediaId& id) { return id == m; }));
    EXPECT_FALSE(tl.validate([](const MediaId&) { return false; }));
    tl.tracks[0].clips[1].id = tl.tracks[0].clips[0].id;  // duplicate id
    EXPECT_FALSE(tl.validate());
}

TEST(Timeline, ActiveClipsSkipHiddenAndDisabled) {
    Timeline tl;
    const MediaId m = MediaId::generate();
    for (int i = 0; i < 3; ++i) {
        Track t;
        t.id = TrackId::generate();
        Clip c = mediaClip(Time::zero(), Time::fromSeconds(10), m);
        c.sourceIn = Time::fromSeconds(i);
        t.clips.push_back(c);
        tl.tracks.push_back(t);
    }
    tl.tracks[1].hidden = true;
    tl.tracks[2].clips[0].enabled = false;
    const auto active = tl.activeClipsAt(Time::fromSeconds(3));
    ASSERT_EQ(active.size(), 1u);
    EXPECT_EQ(active[0].sourceTime, Time::fromSeconds(3));
}

TEST(TimelineJson, RoundTripPreservesEverything) {
    Timeline tl;
    Track t;
    t.id = TrackId::generate();
    t.kind = TrackKind::Video;
    t.name = "Camera";
    Clip c = mediaClip(Time::fromSeconds(1), Time::fromSeconds(9), MediaId::generate());
    c.sourceIn = Time::fromMilliseconds(83);
    c.speed = Rational(3, 2);
    c.linkGroup = LinkGroupId::generate();
    c.transform.position.setKey(Time::zero(), {0.84, 0.8}, Interpolation::EaseInOut);
    c.transform.position.setKey(Time::fromSeconds(1), {0.5, 0.5}, Interpolation::Bezier);
    c.transform.position.keys[1].handles = {0.1, 0.2, 0.3, 0.9};
    c.opacity = 0.75;
    c.crop = Vec4{0.1, 0, 0.1, 0};
    c.audio.gainDb = -3.0;
    c.audio.fadeIn = Time::fromMilliseconds(250);
    c.color.saturation = 0.2;
    EffectInstance e;
    e.id = EffectId::generate();
    e.type = "lectern.blur.gaussian";
    e.params["radius"] = 8.0;
    c.effects.push_back(e);
    t.clips.push_back(c);
    tl.tracks.push_back(t);
    tl.markers.push_back({MarkerId::generate(), Time::fromSeconds(4), "Paused", "#F5A524", MarkerKind::Pause});
    tl.layout.push_back({LayoutRegionId::generate(), {Time::zero(), Time::fromSeconds(10)}, "pip.bottom-right.rounded"});

    const auto j = toJson(tl);
    auto parsed = timelineFromJson(json::parse(json::dump(j)).value());
    ASSERT_TRUE(parsed) << parsed.error().toString();
    EXPECT_EQ(*parsed, tl);
}

TEST(TimelineJson, ErrorsCarryPaths) {
    const auto j = json::parse(R"({"tracks":[{"id":"0199b2a4-6f3e-7c41-9a5e-2b0d7c9e1f00","kind":"video",
        "clips":[{"id":"0199b2a4-6f3e-7c41-9a5e-2b0d7c9e1f01","kind":"media","start":0,"duration":"x"}]}]})")
                       .value();
    auto r = timelineFromJson(j);
    ASSERT_FALSE(r);
    EXPECT_NE(r.error().message().find("timeline.tracks[0].clips[0].duration"), std::string::npos)
        << r.error().message();
}
