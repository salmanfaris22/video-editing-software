#pragma once

// Timeline edit operations (docs/TIMELINE_ENGINE.md §6). Pure functions over
// the document model: no I/O, no Qt. Each succeeds completely or leaves the
// timeline untouched, so the caller can snapshot before and commit after.
//
// Editing model ("magnetic" for recordings): clips of one recording share a
// link group and stay in sync — splitting, deleting and trimming a recording
// segment applies to the whole group and ripples later content. Free clips
// (text, overlays, music) are edited on their own and never ripple.

#include "timeline/Timeline.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lectern::timeline::edit {

/// The media time range a media clip may show: [start, start + duration).
/// nullopt = unknown/unbounded.
using MediaBounds = std::function<std::optional<TimeRange>(const MediaId&)>;

/// Shortest clip an edit may leave behind.
inline constexpr Time kMinClipDuration = Time::fromMilliseconds(40);

/// Splits at `t`. With `clip`: that clip and its link group. Without: every
/// linked clip crossing `t` on unlocked tracks (or, when nothing is linked,
/// every clip crossing `t` on unlocked video/audio tracks). Right halves of a
/// group share a fresh group. Returns how many clips were split (0 = none).
Result<int> splitAt(Timeline& tl, Time t, const std::optional<ClipId>& clip = {});

/// Ripple delete: removes `range` from every unlocked track and closes the
/// gap. Clips are cut or trimmed; later clips, markers and layout regions
/// move left; regions that become adjacent with the same preset merge.
Status removeRange(Timeline& tl, TimeRange range);

/// Removes several ranges as one edit (overlaps merged, applied last-first).
/// Returns the total time removed.
Result<Time> removeRanges(Timeline& tl, std::vector<TimeRange> ranges);

/// Deletes a clip. A linked recording segment is removed together with its
/// group, closing the gap; a free clip leaves a gap.
Status deleteClip(Timeline& tl, const ClipId& id);
/// Removes only this clip on its track (gap remains). Linked groups on other
/// tracks are untouched.
Status deleteClipLocal(Timeline& tl, const ClipId& id);

/// Moves a free clip on its track; the start is clamped between neighbours
/// and zero. A linked recording segment moves with its whole link group (all
/// tracks by the same amount), sliding as far as every track has room.
Status moveClip(Timeline& tl, const ClipId& id, Time newStart);
/// Moves a free clip onto another track of the same kind (or along its own),
/// placed in the gap at `newStart` like moveClip; fails when it does not fit.
Status moveClipToTrack(Timeline& tl, const ClipId& id, const TrackId& track, Time newStart);

/// Moves a clip's left/right edge to `edge`. Linked segments trim as a group
/// and ripple (later content follows); free clips stop at neighbours. Both
/// are limited by the media available (`bounds`) and kMinClipDuration.
Status trimStart(Timeline& tl, const ClipId& id, Time edge, const MediaBounds& bounds = {});
Status trimEnd(Timeline& tl, const ClipId& id, Time edge, const MediaBounds& bounds = {});

/// Layout preset at `t` ("screen.only" when no region covers it).
[[nodiscard]] std::string layoutAt(const Timeline& tl, Time t);
/// One preset for the whole timeline.
void setLayoutAll(Timeline& tl, const std::string& preset);
/// Preset from `t` to the next layout change (splits the region at `t`).
Status setLayoutFrom(Timeline& tl, Time t, const std::string& preset);

/// Clips sharing `clip`'s link group (including itself); just `clip` when unlinked.
[[nodiscard]] std::vector<const Clip*> linkedClips(const Timeline& tl, const ClipId& clip);
/// True for a clip that belongs to a multi-clip link group (a recording segment).
[[nodiscard]] bool isLinkedSegment(const Timeline& tl, const ClipId& clip);

}  // namespace lectern::timeline::edit
