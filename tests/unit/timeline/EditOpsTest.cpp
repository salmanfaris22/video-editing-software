#include "timeline/EditOps.h"

#include <gtest/gtest.h>

#include <map>

using namespace lectern;
using namespace lectern::timeline;

namespace {

Time sec(double s) { return Time::fromSecondsF(s); }

/// A recording as the importer builds it — screen, camera (started 100 ms
/// late) and microphone in one link group — plus a free text overlay and a
/// music bed, two markers and one layout region.
struct Recording {
    Timeline tl;
    std::map<MediaId, TimeRange> media;
    MediaId screenMedia = MediaId::generate();
    MediaId cameraMedia = MediaId::generate();
    MediaId micMedia = MediaId::generate();
    MediaId musicMedia = MediaId::generate();
    LinkGroupId group = LinkGroupId::generate();
    ClipId screen, camera, mic, text, music;

    Recording() {
        screen = addMedia(TrackKind::Video, "Screen", screenMedia, sec(0), sec(10), true);
        camera = addMedia(TrackKind::Video, "Camera", cameraMedia, sec(0.1), sec(9.9), true);
        mic = addMedia(TrackKind::Audio, "Microphone", micMedia, sec(0), sec(10.02), true);
        Track overlay{TrackId::generate(), TrackKind::Overlay, "Text"};
        Clip t;
        t.id = text = ClipId::generate();
        t.kind = ClipKind::Text;
        t.range = {sec(2), sec(2)};
        t.text = TextContent{"Hello", "title", {}};
        EXPECT_TRUE(overlay.insertClip(t));
        tl.tracks.push_back(overlay);
        music = addMedia(TrackKind::Audio, "Music", musicMedia, sec(0), sec(20), false);
        media[musicMedia] = {sec(0), sec(60)};
        tl.markers.push_back({MarkerId::generate(), sec(3), "A", "#F5A524", MarkerKind::User});
        tl.markers.push_back({MarkerId::generate(), sec(8), "B", "#F5A524", MarkerKind::User});
        tl.layout.push_back({LayoutRegionId::generate(), {sec(0), sec(10.02)}, "pip.bottom-right.rounded"});
    }

    ClipId addMedia(TrackKind kind, const std::string& name, const MediaId& m, Time start, Time duration, bool linked) {
        Track track{TrackId::generate(), kind, name};
        Clip c;
        c.id = ClipId::generate();
        c.name = name;
        c.media = m;
        c.range = {start, duration};
        c.sourceIn = start;  // recordings: media time == timeline time
        if (linked) c.linkGroup = group;
        EXPECT_TRUE(track.insertClip(c));
        tl.tracks.push_back(track);
        media[m] = {start, duration};
        return c.id;
    }

    [[nodiscard]] edit::MediaBounds bounds() const {
        return [this](const MediaId& id) -> std::optional<TimeRange> {
            const auto it = media.find(id);
            return it == media.end() ? std::nullopt : std::optional<TimeRange>(it->second);
        };
    }

    Track& track(const std::string& name) {
        for (Track& t : tl.tracks) {
            if (t.name == name) return t;
        }
        throw std::runtime_error("no track " + name);
    }

    /// Media time shown on a track at timeline time t (nullopt = gap).
    std::optional<double> sourceAt(const std::string& name, double t) {
        const Clip* c = track(name).clipAt(sec(t));
        if (!c) return std::nullopt;
        return c->sourceTimeAt(sec(t)).toSecondsF();
    }
};

}  // namespace

TEST(EditOps, SplitAtPlayheadSplitsTheRecordingButNotFreeClips) {
    Recording r;
    auto n = edit::splitAt(r.tl, sec(5));
    ASSERT_TRUE(n) << n.error().toString();
    EXPECT_EQ(*n, 3);
    ASSERT_EQ(r.track("Screen").clips.size(), 2u);
    EXPECT_EQ(r.track("Music").clips.size(), 1u);
    const Clip& left = r.track("Screen").clips[0];
    const Clip& right = r.track("Screen").clips[1];
    EXPECT_EQ(left.range, (TimeRange{sec(0), sec(5)}));
    EXPECT_EQ(right.range.start, sec(5));
    EXPECT_EQ(right.sourceIn, sec(5));
    EXPECT_EQ(left.linkGroup, r.group);
    ASSERT_TRUE(right.linkGroup);
    EXPECT_NE(*right.linkGroup, r.group);  // the right halves form their own segment
    EXPECT_EQ(r.track("Microphone").clips[1].linkGroup, right.linkGroup);
    EXPECT_EQ(r.track("Camera").clips[1].linkGroup, right.linkGroup);
    EXPECT_TRUE(r.tl.validate());
}

TEST(EditOps, SplitSelectedFreeClipOnlyAndRefusesNearEdges) {
    Recording r;
    auto n = edit::splitAt(r.tl, sec(12), r.music);
    ASSERT_TRUE(n);
    EXPECT_EQ(*n, 1);
    EXPECT_EQ(r.track("Music").clips.size(), 2u);
    EXPECT_EQ(r.track("Screen").clips.size(), 1u);
    EXPECT_FALSE(edit::splitAt(r.tl, sec(0.01)));  // 10 ms from the screen clip's start
    EXPECT_EQ(r.track("Screen").clips.size(), 1u);
    auto none = edit::splitAt(r.tl, sec(30));
    ASSERT_TRUE(none);
    EXPECT_EQ(*none, 0);
}

TEST(EditOps, SplitKeepsKeyframesAndFadesWithTheirContent) {
    Recording r;
    Clip& screen = r.track("Screen").clips[0];
    screen.opacity.setKey(sec(1), 0.2);
    screen.opacity.setKey(sec(3), 0.8);
    screen.audio.fadeIn = sec(1);
    screen.audio.fadeOut = sec(1);
    const double atThree = screen.opacity.evaluate(sec(3));
    ASSERT_TRUE(edit::splitAt(r.tl, sec(2)));
    const Clip& left = r.track("Screen").clips[0];
    const Clip& right = r.track("Screen").clips[1];
    EXPECT_DOUBLE_EQ(right.opacity.evaluate(sec(1)), atThree);  // local 1 s of the right half = old 3 s
    EXPECT_EQ(left.audio.fadeIn, sec(1));
    EXPECT_EQ(left.audio.fadeOut, Time::zero());
    EXPECT_EQ(right.audio.fadeIn, Time::zero());
    EXPECT_EQ(right.audio.fadeOut, sec(1));
}

TEST(EditOps, DeletingASegmentRipplesEveryUnlockedTrack) {
    Recording r;
    ASSERT_TRUE(edit::splitAt(r.tl, sec(5)));
    const ClipId rightScreen = r.track("Screen").clips[1].id;
    ASSERT_TRUE(edit::deleteClip(r.tl, rightScreen));
    EXPECT_EQ(r.track("Screen").clips.size(), 1u);
    EXPECT_EQ(r.track("Camera").clips.size(), 1u);
    EXPECT_EQ(r.track("Microphone").clips.size(), 1u);
    EXPECT_EQ(r.track("Microphone").clips[0].range.end(), sec(5));
    // Music lost the removed stretch [5, 10.02) and closes up behind it.
    ASSERT_EQ(r.track("Music").clips.size(), 2u);
    EXPECT_EQ(r.track("Music").clips[1].range.start, sec(5));
    EXPECT_EQ(r.track("Music").clips[1].sourceIn, sec(10.02));
    EXPECT_EQ(r.tl.duration(), sec(20 - 5.02));
    ASSERT_EQ(r.tl.markers.size(), 1u);  // the marker at 8 s was inside the deleted segment
    EXPECT_EQ(r.tl.markers[0].time, sec(3));
    EXPECT_EQ(r.track("Text").clips[0].range, (TimeRange{sec(2), sec(2)}));
    ASSERT_EQ(r.tl.layout.size(), 1u);
    EXPECT_EQ(r.tl.layout[0].range.end(), sec(5));
    EXPECT_TRUE(r.tl.validate());
}

TEST(EditOps, DeletingAFreeClipLeavesAGap) {
    Recording r;
    ASSERT_TRUE(edit::deleteClip(r.tl, r.text));
    EXPECT_TRUE(r.track("Text").clips.empty());
    EXPECT_EQ(r.track("Screen").clips[0].range.duration, sec(10));
}

TEST(EditOps, DeleteClipLocalRemovesOneTrackOnly) {
    Recording r;
    ASSERT_TRUE(edit::splitAt(r.tl, sec(5)));
    const ClipId rightScreen = r.track("Screen").clips[1].id;
    ASSERT_TRUE(edit::deleteClipLocal(r.tl, rightScreen));
    EXPECT_EQ(r.track("Screen").clips.size(), 1u);
    EXPECT_EQ(r.track("Camera").clips.size(), 2u);
    EXPECT_EQ(r.tl.duration(), sec(20));
    EXPECT_TRUE(r.tl.validate());
}

TEST(EditOps, RemoveRangeCutsAndShiftsContentMarkersAndLayout) {
    Recording r;
    ASSERT_TRUE(edit::removeRange(r.tl, {sec(2.5), sec(1)}));
    EXPECT_EQ(r.sourceAt("Screen", 2.4), 2.4);
    EXPECT_NEAR(*r.sourceAt("Screen", 2.5), 3.5, 1e-9);  // the cut joins 2.5 → 3.5
    EXPECT_NEAR(*r.sourceAt("Camera", 5.0), 6.0, 1e-9);
    EXPECT_NEAR(*r.sourceAt("Microphone", 8.0), 9.0, 1e-9);
    // The text overlay [2, 4) loses the middle second.
    const auto& text = r.track("Text").clips;
    ASSERT_EQ(text.size(), 2u);
    EXPECT_EQ(text[0].range, (TimeRange{sec(2), sec(0.5)}));
    EXPECT_EQ(text[1].range, (TimeRange{sec(2.5), sec(0.5)}));
    ASSERT_EQ(r.tl.markers.size(), 1u);
    EXPECT_EQ(r.tl.markers[0].time, sec(7));
    EXPECT_EQ(r.tl.layout[0].range.duration, sec(9.02));
    EXPECT_TRUE(r.tl.validate());
}

TEST(EditOps, RemoveRangesMergesOverlapsAndAppliesLastFirst) {
    Recording r;
    auto removed = edit::removeRanges(r.tl, {{sec(1), sec(1)}, {sec(1.5), sec(1)}, {sec(6), sec(1)}});
    ASSERT_TRUE(removed) << removed.error().toString();
    EXPECT_EQ(*removed, sec(2.5));
    Time total;
    for (const Clip& c : r.track("Screen").clips) total += c.range.duration;
    EXPECT_EQ(total, sec(7.5));
    EXPECT_NEAR(*r.sourceAt("Screen", 0.5), 0.5, 1e-9);
    EXPECT_NEAR(*r.sourceAt("Screen", 1.0), 2.5, 1e-9);
    EXPECT_NEAR(*r.sourceAt("Screen", 5.0), 7.5, 1e-9);
    EXPECT_TRUE(r.tl.validate());
}

TEST(EditOps, LockedTracksAreLeftAlone) {
    Recording r;
    r.track("Music").locked = true;
    ASSERT_TRUE(edit::removeRange(r.tl, {sec(4), sec(2)}));
    EXPECT_EQ(r.track("Music").clips[0].range, (TimeRange{sec(0), sec(20)}));
    EXPECT_EQ(r.track("Screen").clips[1].range.start, sec(4));
}

TEST(EditOps, TrimmingSegmentEdgesRipplesAndCanRevealMediaAgain) {
    Recording r;
    ASSERT_TRUE(edit::splitAt(r.tl, sec(5)));
    const ClipId leftScreen = r.track("Screen").clips[0].id;
    ASSERT_TRUE(edit::trimEnd(r.tl, leftScreen, sec(4), r.bounds()));
    EXPECT_EQ(r.track("Screen").clips[0].range.end(), sec(4));
    EXPECT_EQ(r.track("Screen").clips[1].range.start, sec(4));
    EXPECT_NEAR(*r.sourceAt("Screen", 4.0), 5.0, 1e-9);
    EXPECT_EQ(r.track("Microphone").clips[0].range.end(), sec(4));

    // Drag the edge back out: the trimmed media returns, later content moves right.
    ASSERT_TRUE(edit::trimEnd(r.tl, leftScreen, sec(4.5), r.bounds()));
    EXPECT_EQ(r.track("Screen").clips[0].range.end(), sec(4.5));
    EXPECT_EQ(r.track("Screen").clips[1].range.start, sec(4.5));
    EXPECT_NEAR(*r.sourceAt("Screen", 4.4), 4.4, 1e-9);
    EXPECT_NEAR(*r.sourceAt("Screen", 4.5), 5.0, 1e-9);
    EXPECT_TRUE(r.tl.validate());
}

TEST(EditOps, TrimmingTheFirstSegmentHeadRipplesAndIsBoundedByMedia) {
    Recording r;
    ASSERT_TRUE(edit::trimStart(r.tl, r.screen, sec(1), r.bounds()));
    EXPECT_EQ(r.track("Screen").clips[0].range, (TimeRange{sec(0), sec(9)}));
    EXPECT_EQ(r.track("Screen").clips[0].sourceIn, sec(1));
    EXPECT_EQ(r.track("Camera").clips[0].range.start, sec(0));
    EXPECT_EQ(r.track("Camera").clips[0].sourceIn, sec(1));  // stays in sync with the screen
    // Extending the head again is limited to the media that exists (1 s).
    ASSERT_TRUE(edit::trimStart(r.tl, r.screen, sec(-5), r.bounds()));
    EXPECT_EQ(r.track("Screen").clips[0].sourceIn, sec(0.1));  // the camera's media starts at 0.1 s
    EXPECT_TRUE(r.tl.validate());
}

TEST(EditOps, FreeClipTrimsStopAtNeighboursAndMediaBounds) {
    Recording r;
    Track& overlay = r.track("Text");
    Clip second;
    second.id = ClipId::generate();
    second.kind = ClipKind::Text;
    second.range = {sec(5), sec(1)};
    second.text = TextContent{"Later", "caption", {}};
    ASSERT_TRUE(overlay.insertClip(second));
    ASSERT_TRUE(edit::trimEnd(r.tl, r.text, sec(5.5)));
    EXPECT_EQ(overlay.clips[0].range.end(), sec(5));
    ASSERT_TRUE(edit::trimStart(r.tl, second.id, sec(3)));
    EXPECT_EQ(overlay.clips[1].range.start, sec(5));  // the first clip now ends at 5
    ASSERT_TRUE(edit::trimEnd(r.tl, r.music, sec(90), r.bounds()));
    EXPECT_EQ(r.track("Music").clips[0].range.end(), sec(60));  // all the music there is
    ASSERT_TRUE(edit::trimEnd(r.tl, r.text, sec(1)));
    EXPECT_EQ(overlay.clips[0].range.duration, edit::kMinClipDuration);
}

TEST(EditOps, MovingFreeClipsSnapsIntoGaps) {
    Recording r;
    Track& overlay = r.track("Text");
    Clip blocker;
    blocker.id = ClipId::generate();
    blocker.kind = ClipKind::Text;
    blocker.range = {sec(5), sec(1)};
    blocker.text = TextContent{"Blocker", "caption", {}};
    ASSERT_TRUE(overlay.insertClip(blocker));
    ASSERT_TRUE(edit::moveClip(r.tl, r.text, sec(4.5)));  // would overlap: ends up before the blocker
    EXPECT_EQ(overlay.clips[0].range.start, sec(3));
    ASSERT_TRUE(edit::moveClip(r.tl, r.text, sec(5.5)));  // starts inside the blocker: snaps after it
    EXPECT_EQ(overlay.clips[1].range.start, sec(6));
    ASSERT_TRUE(edit::moveClip(r.tl, r.text, sec(-3)));
    EXPECT_EQ(overlay.clips[0].range.start, sec(0));
    // A recording segment moves with its link group (here the whole recording starts at 0 with nothing before it).
    EXPECT_TRUE(r.tl.validate());
}

TEST(EditOps, RecordingSegmentsMoveTogetherIntoTheGap) {
    Recording r;
    // Split at 4 s, then delete the left part on this track only (gap 0–4 s) the way "Cut: Track" does.
    ASSERT_TRUE(edit::splitAt(r.tl, sec(4)));
    std::vector<ClipId> left;
    for (const Track& t : r.tl.tracks)
        if (!t.clips.empty() && t.clips.front().linkGroup && t.clips.front().range.start < sec(1)) left.push_back(t.clips.front().id);
    for (const ClipId& c : left) ASSERT_TRUE(edit::deleteClipLocal(r.tl, c));
    const Clip* screen = nullptr;
    for (const Clip& c : r.track("Screen").clips) screen = &c;
    ASSERT_TRUE(screen);
    const Time cameraOffset = r.track("Camera").clips.back().range.start - screen->range.start;
    // Drag to 1 s: every track moves by the same amount.
    ASSERT_TRUE(edit::moveClip(r.tl, screen->id, sec(1)));
    EXPECT_EQ(r.track("Screen").clips.back().range.start, sec(1));
    EXPECT_EQ(r.track("Camera").clips.back().range.start - r.track("Screen").clips.back().range.start, cameraOffset);
    EXPECT_EQ(r.track("Microphone").clips.back().range.start, sec(1));
    // Dragging before 0 stops at 0 (the camera, which starts 0.1 s later, keeps its offset).
    ASSERT_TRUE(edit::moveClip(r.tl, r.track("Screen").clips.back().id, sec(-5)));
    EXPECT_EQ(r.track("Screen").clips.back().range.start, sec(0));
    EXPECT_TRUE(r.tl.validate());
    // A locked track of the recording blocks the move.
    r.track("Microphone").locked = true;
    EXPECT_FALSE(edit::moveClip(r.tl, r.track("Screen").clips.back().id, sec(2)));
}

TEST(EditOps, OneClipOfARecordingMovesAloneAndLeavesItsGroup) {
    Recording r;
    // Split at 4 s and make room after the recording, like the timeline in practice.
    ASSERT_TRUE(edit::splitAt(r.tl, sec(4)));
    const Clip camRight = r.track("Camera").clips.back();
    const Time screenStart = r.track("Screen").clips.back().range.start;
    const Time micStart = r.track("Microphone").clips.back().range.start;
    ASSERT_TRUE(edit::isLinkedSegment(r.tl, camRight.id));
    // Move only the camera's right half to 12 s: the screen and mic stay.
    ASSERT_TRUE(edit::moveClipAlone(r.tl, camRight.id, sec(12)));
    const Clip* moved = r.tl.findClip(camRight.id);
    ASSERT_TRUE(moved);
    EXPECT_EQ(moved->range.start, sec(12));
    EXPECT_FALSE(moved->linkGroup);  // it edits on its own now
    EXPECT_EQ(r.track("Screen").clips.back().range.start, screenStart);
    EXPECT_EQ(r.track("Microphone").clips.back().range.start, micStart);
    EXPECT_TRUE(edit::isLinkedSegment(r.tl, r.track("Screen").clips.back().id));  // the rest stays linked
    EXPECT_TRUE(r.tl.validate());
    // A later normal move of the screen no longer drags the camera along.
    ASSERT_TRUE(edit::moveClip(r.tl, r.track("Screen").clips.back().id, sec(4.5)));
    EXPECT_EQ(r.tl.findClip(camRight.id)->range.start, sec(12));
    // It can change tracks (same kind) on its own too.
    r.tl.tracks.push_back({TrackId::generate(), TrackKind::Video, "Camera 2"});
    const TrackId cam2 = r.tl.tracks.back().id;
    ASSERT_TRUE(edit::moveClipAlone(r.tl, camRight.id, sec(1), cam2));
    EXPECT_EQ(r.track("Camera 2").clips.size(), 1u);
    // A failed move (no room) changes nothing, not even the link.
    const Clip screenLeft = r.track("Screen").clips.front();
    const Time blocked = r.track("Camera").clips.front().range.start;
    EXPECT_FALSE(edit::moveClipAlone(r.tl, screenLeft.id, sec(1), r.track("Microphone").id));  // wrong kind
    EXPECT_EQ(r.tl.findClip(screenLeft.id)->linkGroup, screenLeft.linkGroup);
    EXPECT_EQ(r.track("Camera").clips.front().range.start, blocked);
    // Unlinking the second-to-last member frees the last one as well.
    Recording two;
    ASSERT_TRUE(edit::unlinkClip(two.tl, two.camera));
    ASSERT_TRUE(edit::unlinkClip(two.tl, two.mic));
    EXPECT_FALSE(two.tl.findClip(two.screen)->linkGroup);
}

TEST(EditOps, FreeClipsMoveBetweenTracksOfTheSameKind) {
    Recording r;
    const TrackId first = r.track("Text").id;
    r.tl.tracks.push_back({TrackId::generate(), TrackKind::Overlay, "Text 2"});
    const TrackId second = r.tl.tracks.back().id;
    const Time duration = r.tl.findClip(r.text)->range.duration;

    ASSERT_TRUE(edit::moveClipToTrack(r.tl, r.text, second, sec(6)));
    EXPECT_TRUE(r.tl.findTrack(first)->clips.empty());
    ASSERT_EQ(r.tl.findTrack(second)->clips.size(), 1U);
    EXPECT_EQ(r.tl.findTrack(second)->clips[0].range.start, sec(6));
    EXPECT_EQ(r.tl.findTrack(second)->clips[0].range.duration, duration);

    // An audio track is the wrong kind; a recording segment never floats; a
    // locked target refuses. Nothing changes on failure.
    EXPECT_FALSE(edit::moveClipToTrack(r.tl, r.text, r.track("Microphone").id, sec(0)));
    EXPECT_FALSE(edit::moveClipToTrack(r.tl, r.screen, r.track("Camera").id, sec(0)));
    r.tl.findTrack(first)->locked = true;
    EXPECT_FALSE(edit::moveClipToTrack(r.tl, r.text, first, sec(0)));
    r.tl.findTrack(first)->locked = false;
    EXPECT_EQ(r.tl.findTrack(second)->clips.size(), 1U);

    // Dropped inside a clip on the target: lands right after it, like moveClip.
    Clip wide;
    wide.id = ClipId::generate();
    wide.kind = ClipKind::Text;
    wide.range = {sec(0), sec(5)};
    wide.text = TextContent{"Wide", "caption", {}};
    ASSERT_TRUE(r.tl.findTrack(first)->insertClip(wide));
    Clip after = wide;
    after.id = ClipId::generate();
    after.range = {sec(6), sec(10)};
    ASSERT_TRUE(r.tl.findTrack(first)->insertClip(after));
    // The 1 s gap at 5–6 s is too short for the 2 s clip: refused, nothing moves.
    EXPECT_FALSE(edit::moveClipToTrack(r.tl, r.text, first, sec(5)));
    EXPECT_EQ(r.tl.findTrack(second)->clips.size(), 1U);
    ASSERT_TRUE(edit::moveClipToTrack(r.tl, r.text, first, sec(17)));
    EXPECT_EQ(r.tl.findTrack(first)->clips.back().range.start, sec(17));
    EXPECT_TRUE(r.tl.findTrack(second)->clips.empty());
    // Same track: an ordinary move.
    ASSERT_TRUE(edit::moveClipToTrack(r.tl, r.text, first, sec(20)));
    EXPECT_EQ(r.tl.findTrack(first)->clips.back().range.start, sec(20));
    EXPECT_TRUE(r.tl.validate());
}

TEST(EditOps, LayoutChangesFromThePlayheadAndMerges) {
    Recording r;
    edit::setLayoutAll(r.tl, "pip.bottom-right.rounded");
    ASSERT_TRUE(edit::setLayoutFrom(r.tl, sec(4), "screen.only"));
    EXPECT_EQ(edit::layoutAt(r.tl, sec(2)), "pip.bottom-right.rounded");
    EXPECT_EQ(edit::layoutAt(r.tl, sec(5)), "screen.only");
    ASSERT_TRUE(edit::setLayoutFrom(r.tl, sec(6), "pip.bottom-right.rounded"));
    EXPECT_EQ(r.tl.layout.size(), 3u);
    ASSERT_TRUE(edit::setLayoutFrom(r.tl, sec(4), "pip.bottom-right.rounded"));
    ASSERT_EQ(r.tl.layout.size(), 1u);  // all the same again: merged
    EXPECT_EQ(r.tl.layout[0].range.start, sec(0));
    EXPECT_EQ(edit::layoutAt(r.tl, sec(100)), "pip.bottom-right.rounded");
    EXPECT_TRUE(r.tl.validate());
}
