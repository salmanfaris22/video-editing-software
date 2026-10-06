#include "capture/VideoPacer.h"

#include <gtest/gtest.h>

#include <vector>

using namespace lectern;
using namespace lectern::capture;

namespace {

struct Emitted {
    std::int64_t slot;
    int frame;
    bool duplicate;
    bool keyframe;
};

auto dupInt = [](const int& f) { return f; };
using Pacer = VideoPacer<int, decltype(dupInt)>;

Pacer makePacer(FrameRate fps, std::optional<Time> maxHold = std::nullopt) {
    return Pacer(Pacer::Config{fps, Time::fromMilliseconds(100), maxHold}, dupInt);
}

Time frameTime(int index, double fps, double offsetMs = 0) {
    return Time::fromSecondsF(index / fps + offsetMs / 1000.0);
}

}  // namespace

TEST(VideoPacer, MatchingRateEmitsEveryFrameOnce) {
    auto p = makePacer(FrameRate(30, 1));
    std::vector<Emitted> out;
    auto emit = [&](std::int64_t s, int&& f, bool d, bool k) { out.push_back({s, f, d, k}); };
    for (int i = 0; i < 90; ++i) p.push(i, frameTime(i, 30, (i % 3 - 1) * 4.0), emit);  // ±4 ms jitter
    p.finish(Time::fromSeconds(3), emit);
    ASSERT_EQ(out.size(), 90u);
    for (int i = 0; i < 90; ++i) {
        EXPECT_EQ(out[i].slot, i);
        EXPECT_EQ(out[i].frame, i);
        EXPECT_FALSE(out[i].duplicate);
    }
    EXPECT_EQ(p.counters().duplicated, 0u);
    EXPECT_EQ(p.counters().decimated, 0u);
}

TEST(VideoPacer, SlowSourceIsUpsampledWithDuplicates) {
    auto p = makePacer(FrameRate(30, 1));
    std::vector<Emitted> out;
    auto emit = [&](std::int64_t s, int&& f, bool d, bool k) { out.push_back({s, f, d, k}); };
    for (int i = 0; i < 25; ++i) p.push(i, frameTime(i, 25), emit);  // 1 s of 25 fps
    p.finish(Time::fromSeconds(1), emit);
    ASSERT_EQ(out.size(), 30u);
    EXPECT_EQ(p.counters().real, 25u);
    EXPECT_EQ(p.counters().duplicated, 5u);
    for (std::size_t i = 0; i < out.size(); ++i) EXPECT_EQ(out[i].slot, static_cast<std::int64_t>(i));
}

TEST(VideoPacer, FastSourceIsDecimated) {
    auto p = makePacer(FrameRate(30, 1));
    std::vector<Emitted> out;
    auto emit = [&](std::int64_t s, int&& f, bool d, bool k) { out.push_back({s, f, d, k}); };
    for (int i = 0; i < 60; ++i) p.push(i, frameTime(i, 60), emit);
    p.finish(Time::fromSeconds(1), emit);
    EXPECT_EQ(out.size(), 30u);
    // Pairs of 60 fps frames share a slot; the last frame rounds past the end.
    EXPECT_GE(p.counters().decimated, 29u);
    EXPECT_EQ(p.counters().duplicated, 0u);
}

TEST(VideoPacer, IdleScreenDuplicatesOnTimeAndSeedFillsSlotZero) {
    auto p = makePacer(FrameRate(10, 1));
    std::vector<Emitted> out;
    auto emit = [&](std::int64_t s, int&& f, bool d, bool k) { out.push_back({s, f, d, k}); };
    p.seed(-1, Time::fromMilliseconds(-500));  // static screen before T0
    p.advance(Time::fromMilliseconds(120), emit);  // slot 0 closes at 50 ms + 100 ms allowance
    EXPECT_TRUE(out.empty());
    p.advance(Time::fromMilliseconds(151), emit);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].frame, -1);
    p.advance(Time::fromSeconds(2), emit);  // two seconds of nothing changing
    EXPECT_GE(out.size(), 18u);
    for (const auto& e : out) EXPECT_EQ(e.frame, -1);
}

TEST(VideoPacer, MaxHoldLeavesGapsForStalledCamera) {
    auto p = makePacer(FrameRate(10, 1), Time::fromMilliseconds(300));
    std::vector<Emitted> out;
    auto emit = [&](std::int64_t s, int&& f, bool d, bool k) { out.push_back({s, f, d, k}); };
    p.push(0, Time::zero(), emit);
    p.push(1, Time::fromMilliseconds(100), emit);
    // Camera stalls for 2 s, then resumes.
    p.push(2, Time::fromMilliseconds(2100), emit);
    p.finish(Time::fromMilliseconds(2200), emit);
    EXPECT_GT(p.counters().gapSlots, 10u);
    EXPECT_LE(p.counters().duplicated, 3u);
    EXPECT_EQ(out.back().frame, 2);
}

TEST(VideoPacer, LateFramesRefreshDuplicatesAndKeyframeRequestsApply) {
    auto p = makePacer(FrameRate(10, 1));
    std::vector<Emitted> out;
    auto emit = [&](std::int64_t s, int&& f, bool d, bool k) { out.push_back({s, f, d, k}); };
    p.push(0, Time::zero(), emit);
    p.advance(Time::fromMilliseconds(500), emit);  // slots 0..3 emitted
    p.push(7, Time::fromMilliseconds(100), emit);  // late for slot 1
    EXPECT_EQ(p.counters().late, 1u);
    p.requestKeyframe();
    p.advance(Time::fromMilliseconds(700), emit);
    const auto it = std::find_if(out.begin(), out.end(), [](const Emitted& e) { return e.keyframe; });
    ASSERT_NE(it, out.end());
    EXPECT_EQ(it->frame, 7);  // duplicates now use the freshest content
}
