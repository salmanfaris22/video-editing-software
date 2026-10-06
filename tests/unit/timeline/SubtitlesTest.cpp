#include "timeline/Subtitles.h"

#include <gtest/gtest.h>

using namespace lectern;
using namespace lectern::timeline;

namespace {
Time ms(std::int64_t v) { return Time::fromMilliseconds(v); }
}  // namespace

TEST(Subtitles, ParsesSrtWithBomCrlfAndMultipleLines) {
    const std::string srt =
        "\xEF\xBB\xBF"
        "1\r\n00:00:01,000 --> 00:00:03,500\r\nHello there\r\nsecond line\r\n\r\n"
        "2\r\n00:00:04,250 --> 00:00:05,000\r\n<i>Bye</i> &amp; thanks\r\n";
    auto cues = parseSubtitles(srt);
    ASSERT_TRUE(cues) << cues.error().toString();
    ASSERT_EQ(cues->size(), 2u);
    EXPECT_EQ((*cues)[0].range, TimeRange::fromStartEnd(ms(1000), ms(3500)));
    EXPECT_EQ((*cues)[0].text, "Hello there\nsecond line");
    EXPECT_EQ((*cues)[1].range, TimeRange::fromStartEnd(ms(4250), ms(5000)));
    EXPECT_EQ((*cues)[1].text, "Bye & thanks");
}

TEST(Subtitles, ParsesWebVttWithSettingsIdsAndShortTimestamps) {
    const std::string vtt =
        "WEBVTT - lesson\n\nNOTE written by hand\n\n"
        "intro\n00:01.5 --> 00:02.000 align:center line:90%\n<c.yellow>First</c>\n\n"
        "01:00:00.000 --> 01:00:01.000\nAn hour in\n";
    auto cues = parseSubtitles(vtt);
    ASSERT_TRUE(cues) << cues.error().toString();
    ASSERT_EQ(cues->size(), 2u);
    EXPECT_EQ((*cues)[0].range, TimeRange::fromStartEnd(ms(1500), ms(2000)));
    EXPECT_EQ((*cues)[0].text, "First");
    EXPECT_EQ((*cues)[1].range.start, Time::fromSeconds(3600));
}

TEST(Subtitles, RejectsFilesWithoutCuesAndSkipsBrokenOnes) {
    EXPECT_FALSE(parseSubtitles("just some text\nwithout timing\n"));
    EXPECT_FALSE(parseSubtitles(""));
    auto cues = parseSubtitles("1\n00:00:05,000 --> 00:00:04,000\nbackwards\n\n2\n00:00:06,000 --> 00:00:07,000\nok\n");
    ASSERT_TRUE(cues);
    ASSERT_EQ(cues->size(), 1u);
    EXPECT_EQ((*cues)[0].text, "ok");
}

TEST(Subtitles, SrtAndVttRoundTrip) {
    const std::vector<SubtitleCue> cues = {{TimeRange::fromStartEnd(ms(0), ms(1234)), "One"},
                                           {TimeRange::fromStartEnd(ms(3'661'005), ms(3'662'000)),
                                            "Two\nlines"}};
    auto srt = parseSubtitles(writeSrt(cues));
    ASSERT_TRUE(srt);
    EXPECT_EQ(*srt, cues);
    auto vtt = parseSubtitles(writeVtt(cues));
    ASSERT_TRUE(vtt);
    EXPECT_EQ(*vtt, cues);
    EXPECT_NE(writeSrt(cues).find("01:01:01,005 --> 01:01:02,000"), std::string::npos);
}

TEST(Subtitles, CuesGoOntoASubtitleTrackWithoutOverlaps) {
    Timeline tl;
    const int added = addSubtitleCues(tl, {{TimeRange::fromStartEnd(ms(1000), ms(3000)), "A"},
                                           {TimeRange::fromStartEnd(ms(2000), ms(4000)), "B overlaps A"},
                                           {TimeRange::fromStartEnd(ms(5000), ms(6000)), "C"}});
    EXPECT_EQ(added, 3);
    ASSERT_EQ(tl.tracks.size(), 1u);
    EXPECT_EQ(tl.tracks[0].kind, TrackKind::Subtitle);
    const auto back = subtitleCues(tl);
    ASSERT_EQ(back.size(), 3u);
    EXPECT_EQ(back[1].range, TimeRange::fromStartEnd(ms(3000), ms(4000)));  // shortened to fit after A
    EXPECT_EQ(back[2].text, "C");
    EXPECT_TRUE(tl.validate());
}
