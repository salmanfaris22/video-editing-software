#include "capture/SessionClock.h"

#include <gtest/gtest.h>

using namespace lectern;
using namespace lectern::capture;

namespace {
constexpr std::int64_t kSec = 1'000'000'000;
}

TEST(SessionClock, MapsHostTimeAcrossPauses) {
    ManualClock host(100 * kSec);
    SessionClock clock(host);
    EXPECT_FALSE(clock.toSessionTime(100 * kSec));  // not started

    const std::int64_t t0 = clock.start();
    EXPECT_EQ(t0, 100 * kSec);
    EXPECT_FALSE(clock.toSessionTime(t0 - 1));
    EXPECT_EQ(clock.toSessionTime(t0 + 2 * kSec), Time::fromSeconds(2));

    host.set(105 * kSec);
    clock.pause();
    EXPECT_EQ(clock.elapsed(), Time::fromSeconds(5));
    host.set(109 * kSec);
    EXPECT_EQ(clock.elapsed(), Time::fromSeconds(5));  // frozen while paused
    EXPECT_FALSE(clock.toSessionTime(107 * kSec));     // inside the pause
    // Captured before the pause but arriving late: still recorded.
    EXPECT_EQ(clock.toSessionTime(105 * kSec - 10'000'000), Time::fromMilliseconds(4990));

    host.set(110 * kSec);
    clock.resume();
    EXPECT_EQ(clock.toSessionTime(110 * kSec), Time::fromSeconds(5));
    EXPECT_EQ(clock.toSessionTime(112 * kSec), Time::fromSeconds(7));

    host.set(115 * kSec);
    clock.stop();
    EXPECT_EQ(clock.stopSessionTime(), Time::fromSeconds(10));
    EXPECT_FALSE(clock.toSessionTime(115 * kSec));  // at/after S
    EXPECT_EQ(clock.toSessionTime(115 * kSec - 1), Time::fromNanoseconds(10 * kSec - 1));
    ASSERT_EQ(clock.pauses().size(), 1u);
    EXPECT_EQ(clock.pauses()[0].sessionTime, Time::fromSeconds(5));
    EXPECT_EQ(*clock.pauses()[0].hostEnd, 110 * kSec);
}

TEST(SessionClock, ActiveSpansSplitAChunkAroundAPause) {
    ManualClock host(0);
    SessionClock clock(host);
    clock.start();
    host.set(10 * kSec);
    clock.pause();
    host.set(12 * kSec);
    clock.resume();

    std::vector<SessionClock::Span> spans;
    clock.forEachActiveSpan(9 * kSec, 13 * kSec, [&](const SessionClock::Span& s) { spans.push_back(s); });
    ASSERT_EQ(spans.size(), 2u);
    EXPECT_EQ(spans[0].hostBegin, 9 * kSec);
    EXPECT_EQ(spans[0].hostEnd, 10 * kSec);
    EXPECT_EQ(spans[0].sessionBegin, Time::fromSeconds(9));
    EXPECT_EQ(spans[1].hostBegin, 12 * kSec);
    EXPECT_EQ(spans[1].hostEnd, 13 * kSec);
    EXPECT_EQ(spans[1].sessionBegin, Time::fromSeconds(10));
}

TEST(SessionClock, StopWhilePausedEndsAtPausePoint) {
    ManualClock host(0);
    SessionClock clock(host);
    clock.start();
    host.set(3 * kSec);
    clock.pause();
    host.set(8 * kSec);
    clock.stop();
    EXPECT_EQ(clock.stopSessionTime(), Time::fromSeconds(3));
    EXPECT_EQ(clock.state(), SessionClock::State::Stopped);
    EXPECT_EQ(*clock.pauses()[0].hostEnd, 8 * kSec);
}
