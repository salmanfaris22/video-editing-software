#include "audio/DriftController.h"
#include "audio/LevelMeter.h"
#include "audio/SpliceDelayLine.h"
#include "audio/TimestampSmoother.h"

#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <vector>

using namespace lectern::audio;

TEST(TimestampSmoother, MeasuresDriftUnderJitter) {
    TimestampSmoother::Config cfg;
    cfg.nominalRate = 48'000;
    TimestampSmoother s(cfg);
    const double driftPpm = 250.0;  // device runs fast
    const double actualRate = 48'000 * (1 + driftPpm * 1e-6);
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> jitter(-1e6, 1e6);  // ±1 ms
    double maxResidual = 0;
    for (int chunk = 0; chunk < 3000; ++chunk) {  // 30 s of 10 ms chunks
        const std::int64_t index = chunk * 480LL;
        const double trueHost = 5e9 + index * 1e9 / actualRate;
        const auto u = s.add(index, static_cast<std::int64_t>(trueHost + jitter(rng)));
        ASSERT_FALSE(u.discontinuity);
        if (chunk > 500) maxResidual = std::max(maxResidual, std::fabs(u.smoothedHostNs - trueHost));
    }
    EXPECT_NEAR(s.driftPpm(), driftPpm, 15.0);
    // Smoothed timestamps are far more accurate than the ±1 ms raw jitter.
    EXPECT_LT(maxResidual, 250'000.0) << "max residual " << maxResidual / 1e6 << " ms";
}

TEST(TimestampSmoother, DetectsDiscontinuityAndKeepsSlope) {
    TimestampSmoother s({48'000.0});
    for (int i = 0; i < 200; ++i) s.add(i * 480LL, static_cast<std::int64_t>(i * 1e7));
    const double slope = s.samplePeriodNs();
    // 200 ms of samples were lost: timestamps jump but the index does not.
    const auto u = s.add(200 * 480LL, static_cast<std::int64_t>(200 * 1e7 + 200e6));
    EXPECT_TRUE(u.discontinuity);
    EXPECT_NEAR(u.residualNs, 200e6, 1e5);
    EXPECT_EQ(s.discontinuities(), 1u);
    EXPECT_DOUBLE_EQ(s.samplePeriodNs(), slope);
}

TEST(DriftController, SoftCorrectionIsClampedAndHardCorrectionsTrigger) {
    DriftController dc({48'000, 960, 48'000, 0.005});
    auto d = dc.evaluate(1000.0, 1010.0);  // 10 samples ahead
    EXPECT_EQ(d.action, DriftController::Action::Soft);
    EXPECT_EQ(d.samples, -10);
    d = dc.evaluate(0.0, 900.0);  // within threshold but large: clamped to 0.5 %
    EXPECT_EQ(d.action, DriftController::Action::Soft);
    EXPECT_EQ(d.samples, -240);
    d = dc.evaluate(10'000.0, 8'000.0);  // 2000 behind → silence
    EXPECT_EQ(d.action, DriftController::Action::InsertSilence);
    EXPECT_EQ(d.samples, 2000);
    d = dc.evaluate(8'000.0, 10'000.0);  // 2000 ahead → drop
    EXPECT_EQ(d.action, DriftController::Action::DropInput);
    EXPECT_EQ(dc.hardCorrections(), 2u);
}

TEST(SpliceDelayLine, DelaysPassesThroughAndFades) {
    SpliceDelayLine line(1, 4, 4);
    std::vector<float> out;
    auto emit = [&](const float* d, int n) { out.insert(out.end(), d, d + n); };
    std::vector<float> ones(10, 1.0f);
    line.push(ones.data(), 10, emit);
    EXPECT_EQ(out.size(), 6u);  // 4 held back
    EXPECT_EQ(line.bufferedFrames(), 4);
    line.fadeOutTail();
    line.flush(emit);
    ASSERT_EQ(out.size(), 10u);
    EXPECT_FLOAT_EQ(out[5], 1.0f);
    EXPECT_FLOAT_EQ(out[9], 0.0f);  // last sample before the cut is silent
    EXPECT_LT(out[8], out[7]);

    out.clear();
    line.armFadeIn();
    line.push(ones.data(), 10, emit);
    line.flush(emit);
    EXPECT_FLOAT_EQ(out[0], 0.0f);  // fade-in starts at zero
    EXPECT_FLOAT_EQ(out[9], 1.0f);
    EXPECT_EQ(line.truncateTail(3), 0);
}

TEST(LevelMeter, PeakRmsAndClip) {
    LevelMeter m(48'000);
    std::vector<float> sine(4800);
    for (std::size_t i = 0; i < sine.size(); ++i) sine[i] = 0.5f * std::sin(2 * 3.14159265 * 440 * i / 48'000.0);
    for (int i = 0; i < 20; ++i) m.process(sine.data(), 4800, 1, i);
    const auto snap = m.snapshot();
    EXPECT_EQ(snap.channels, 1);
    EXPECT_NEAR(snap.channel[0].peakDb, -6.02f, 0.3f);
    EXPECT_NEAR(snap.channel[0].rmsDb, -9.03f, 0.5f);
    EXPECT_FALSE(snap.channel[0].clipped);
    std::vector<float> loud(480, 1.0f);
    m.process(loud.data(), 480, 1, 99);
    EXPECT_TRUE(m.snapshot().channel[0].clipped);
}
