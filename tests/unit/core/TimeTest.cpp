#include "core/Time.h"

#include <gtest/gtest.h>

#include <limits>
#include <random>

using namespace lectern;

TEST(MulDiv, RoundingModes) {
    EXPECT_EQ(mulDiv(7, 1, 2, Rounding::Floor), 3);
    EXPECT_EQ(mulDiv(7, 1, 2, Rounding::Ceil), 4);
    EXPECT_EQ(mulDiv(7, 1, 2, Rounding::Nearest), 4);  // half away from zero
    EXPECT_EQ(mulDiv(7, 1, 2, Rounding::TowardZero), 3);
    EXPECT_EQ(mulDiv(-7, 1, 2, Rounding::Floor), -4);
    EXPECT_EQ(mulDiv(-7, 1, 2, Rounding::Ceil), -3);
    EXPECT_EQ(mulDiv(-7, 1, 2, Rounding::Nearest), -4);
    EXPECT_EQ(mulDiv(-7, 1, 2, Rounding::TowardZero), -3);
    EXPECT_EQ(mulDiv(5, 1, 3, Rounding::Nearest), 2);
}

TEST(MulDiv, LargeValuesUse128BitIntermediate) {
    // 3 hours in ns × ticks/s would overflow 64-bit if multiplied naively.
    const std::int64_t ns = 3LL * 3600 * 1'000'000'000;
    EXPECT_EQ(mulDiv(ns, Time::kTicksPerSecond, 1'000'000'000, Rounding::Nearest), 3LL * 3600 * Time::kTicksPerSecond);
    EXPECT_EQ(mulDiv(std::numeric_limits<std::int64_t>::max(), 2, 1, Rounding::Nearest),
              std::numeric_limits<std::int64_t>::max());  // saturates
}

TEST(MulDiv, PortableMatchesNative) {
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<std::int64_t> big(-(1LL << 62), 1LL << 62);
    std::uniform_int_distribution<std::int64_t> den(1, 1LL << 40);
    for (int i = 0; i < 20000; ++i) {
        const std::int64_t a = big(rng);
        const std::int64_t b = i % 3 == 0 ? den(rng) : (i % 3 == 1 ? 705'600'000 : 48'000);
        const std::int64_t c = den(rng);
        for (Rounding r : {Rounding::Floor, Rounding::Ceil, Rounding::Nearest, Rounding::TowardZero}) {
            ASSERT_EQ(mulDiv(a, b, c, r), detail::mulDivPortable(a, b, c, r)) << a << " " << b << " " << c;
        }
    }
}

TEST(Rational, NormalizesAndCompares) {
    EXPECT_EQ(Rational(2, 4), Rational(1, 2));
    EXPECT_EQ(Rational(1, -2), Rational(-1, 2));
    EXPECT_FALSE(Rational(1, 0).isValid());
    EXPECT_LT(Rational(1, 3), Rational(1, 2));
    EXPECT_GT(Rational(30000, 1001), Rational(29, 1));
    EXPECT_EQ(Rational(3, 2) * Rational(4, 9), Rational(2, 3));
    EXPECT_EQ(Rational::parse("30000/1001"), Rational(30000, 1001));
    EXPECT_EQ(Rational::parse("25"), Rational(25, 1));
    EXPECT_FALSE(Rational::parse("x/2").has_value());
    EXPECT_EQ(Rational::fromDouble(29.97002997, 1001), Rational(30000, 1001));
}

TEST(Time, FrameDurationsAreExactForCommonRates) {
    for (const FrameRate& r : {FrameRate::k23_976, FrameRate::k24, FrameRate::k25, FrameRate::k29_97, FrameRate::k30,
                               FrameRate::k50, FrameRate::k59_94, FrameRate::k60, FrameRate(120, 1), FrameRate(48, 1)}) {
        const Time d = r.frameDuration();
        // Exactness: num frames of duration d add up to exactly den seconds.
        EXPECT_EQ(d * r.rational().num(), Time::fromSeconds(r.rational().den())) << r.rational().toString();
        EXPECT_EQ(r.frameStart(1000), d * 1000);
    }
}

TEST(Time, SampleBoundariesAreExact) {
    for (int rate : {8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 192000}) {
        EXPECT_EQ(Time::fromSamples(rate, rate), Time::fromSeconds(1)) << rate;
        EXPECT_EQ(Time::fromSamples(1, rate) * rate, Time::fromSeconds(1)) << rate;
        EXPECT_EQ(Time::fromSeconds(7).toSamples(rate), 7LL * rate);
    }
}

TEST(Time, Conversions) {
    EXPECT_EQ(Time::fromMilliseconds(1500).toMilliseconds(), 1500);
    EXPECT_EQ(Time::fromNanoseconds(1'000'000'000), Time::fromSeconds(1));
    EXPECT_EQ(Time::fromRational(90000, Rational(1, 90000)), Time::fromSeconds(1));
    EXPECT_EQ(Time::fromSeconds(2).toRational(Rational(1, 1000)), 2000);
    EXPECT_EQ(Time::fromSeconds(1).toRational(Rational(1001, 30000), Rounding::Floor), 29);
    EXPECT_EQ(Time::fromSeconds(10).scaled(Rational(3, 2)), Time::fromSeconds(15));
    EXPECT_EQ(Time::fromSecondsF(0.5), Time::fromMilliseconds(500));
}

TEST(FrameRate, IndexAndSnap) {
    const FrameRate ntsc = FrameRate::k29_97;
    const Time t = ntsc.frameStart(300);
    EXPECT_EQ(ntsc.frameIndexAt(t), 300);
    EXPECT_EQ(ntsc.frameIndexAt(t - Time::fromTicks(1)), 299);
    EXPECT_EQ(ntsc.snap(t + Time::fromTicks(5)), t);
    EXPECT_EQ(FrameRate::k30.frameIndexAt(Time::fromMilliseconds(49), Rounding::Nearest), 1);
    EXPECT_EQ(FrameRate::k30.frameIndexAt(Time::fromMilliseconds(51), Rounding::Nearest), 2);
}

TEST(TimeRange, ContainsAndIntersects) {
    const TimeRange a{Time::fromSeconds(1), Time::fromSeconds(2)};  // [1, 3)
    EXPECT_TRUE(a.contains(Time::fromSeconds(1)));
    EXPECT_FALSE(a.contains(Time::fromSeconds(3)));
    const TimeRange b{Time::fromSeconds(3), Time::fromSeconds(1)};
    EXPECT_FALSE(a.intersects(b));
    const auto i = a.intersection({Time::fromSeconds(2), Time::fromSeconds(5)});
    ASSERT_TRUE(i);
    EXPECT_EQ(i->start, Time::fromSeconds(2));
    EXPECT_EQ(i->end(), Time::fromSeconds(3));
}

TEST(TimeFormatting, DurationAndTimecode) {
    EXPECT_EQ(formatDuration(Time::fromSeconds(65)), "1:05");
    EXPECT_EQ(formatDuration(Time::fromSeconds(3725)), "1:02:05");
    EXPECT_EQ(formatTimecode(FrameRate::k30.frameStart(30 * 61 + 7), FrameRate::k30), "00:01:01:07");
    EXPECT_EQ(Time::fromMilliseconds(61'250).toString(), "01:01.250");
}
