#include "capture/ArrivalAnchoredClock.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <random>

using namespace lectern::capture;

namespace {
constexpr std::int64_t kMs = 1'000'000;
constexpr std::int64_t kFrameNs = 33'333'333;
constexpr std::int64_t kEpoch = 5'000'000 * kMs;  // host time of media time 0
}  // namespace

TEST(ArrivalAnchoredClock, RemovesDeliveryJitterAndKeepsMinimumLatency) {
    ArrivalAnchoredClock clock;
    std::mt19937 rng(7);
    std::uniform_int_distribution<std::int64_t> jitter(0, 8 * kMs);
    std::int64_t previous = 0;
    for (int i = 0; i < 300; ++i) {
        const std::int64_t media = i * kFrameNs;
        const std::int64_t captured = kEpoch + media;
        // 12 ms minimum delivery latency; every 10th frame arrives without extra delay.
        const std::int64_t arrival = captured + 12 * kMs + (i % 10 == 0 ? 0 : jitter(rng));
        const std::int64_t host = clock.map(media, arrival);
        if (i >= 10) {
            EXPECT_GE(host - captured, 12 * kMs - 1) << i;
            EXPECT_LE(host - captured, 12 * kMs + 250'000) << i;  // ≤ 10 samples of leak
            EXPECT_NEAR(static_cast<double>(host - previous), static_cast<double>(kFrameNs), 250'000.0) << i;
        }
        previous = host;
    }
    EXPECT_EQ(clock.reanchors(), 0u);
}

TEST(ArrivalAnchoredClock, FollowsDeviceClockDriftInBothDirections) {
    for (const double ppm : {-300.0, 300.0}) {
        ArrivalAnchoredClock clock;
        double worst = 0;
        for (int i = 0; i < 9000; ++i) {  // five minutes at 30 fps
            const std::int64_t elapsed = i * kFrameNs;
            const std::int64_t captured = kEpoch + elapsed;
            const auto media = static_cast<std::int64_t>(static_cast<double>(elapsed) * (1.0 + ppm * 1e-6));
            const std::int64_t arrival = captured + 10 * kMs + (i % 5 == 0 ? 0 : 3 * kMs);
            const std::int64_t host = clock.map(media, arrival);
            if (i >= 30) worst = std::max(worst, std::abs(static_cast<double>(host - captured - 10 * kMs)));
        }
        EXPECT_LT(worst, 0.5 * kMs) << ppm << " ppm";
        EXPECT_EQ(clock.reanchors(), 0u);
    }
}

TEST(ArrivalAnchoredClock, ReanchorsWhenTheMediaTimelineJumps) {
    ArrivalAnchoredClock clock;
    std::int64_t arrival = kEpoch;
    for (int i = 0; i < 30; ++i, arrival += kFrameNs) (void)clock.map(i * kFrameNs, arrival);
    // The device restarted its timeline at zero; host times keep following arrival.
    const std::int64_t host = clock.map(0, arrival);
    EXPECT_EQ(clock.reanchors(), 1u);
    EXPECT_EQ(host, arrival);
    EXPECT_EQ(clock.map(kFrameNs, arrival + kFrameNs), arrival + kFrameNs);
}

TEST(ArrivalAnchoredClock, PlausibleCaptureTimeWindow) {
    const std::int64_t arrival = kEpoch;
    EXPECT_TRUE(plausibleCaptureTime(arrival - 30 * kMs, arrival));
    EXPECT_TRUE(plausibleCaptureTime(arrival + 4 * kMs, arrival));   // clock-read jitter
    EXPECT_FALSE(plausibleCaptureTime(arrival + 10 * kMs, arrival)); // from the future
    EXPECT_FALSE(plausibleCaptureTime(arrival - 2000 * kMs, arrival));
    EXPECT_FALSE(plausibleCaptureTime(1234, arrival));                // a different epoch
}
