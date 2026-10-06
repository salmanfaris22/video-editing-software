# Timeline Engine

The timeline is the editor's authoritative model: tracks, clips, layouts,
keyframes, markers. It lives in Qt-free C++ (`src/timeline/`, owned by
`project::Project`). The UI renders and manipulates it only through commands.

Status (2026-10-05): **implemented and tested**: data model, time math,
serialization, edit operations, undo/redo, layouts by section, subtitles,
markers, silence removal and the interactive QML timeline. §0 describes what
is built; the sections after it are the original design. Where the two
differ, §0 says so.

---

## 0. As built

**Edit operations** (`src/timeline/EditOps.h`) are pure functions over the
document. Each one either succeeds completely or leaves the timeline
untouched. All are tested in `EditOpsTest`, which checks picture source times,
markers, layout regions and `validate()` after every operation.

* **Recordings are magnetic.** The clips of one recording share a link group:
  - `splitAt` cuts the whole group, and the right halves form a new group.
  - `deleteClip` on a segment ripple-deletes it on every unlocked track.
  - `trimStart`/`trimEnd` on a segment ripple, and can reveal trimmed media
    again, limited by what the media files contain. A segment can grow at
    its head even at time zero; later content moves right.
* **Free clips** (text, overlays, music, subtitles) are trimmed between their
  neighbours, `moveClip` snaps them into the gap they land in, and deleting
  one leaves a gap.
* **`removeRange` / `removeRanges`** do ripple cuts on every unlocked track:
  - Clips are cut or trimmed; later clips, markers and layout regions move left.
  - Keyframes and fades travel with their content: a cut point has no fades,
    and keys are shifted.
  - Leftover slivers under 40 ms are dropped.
  - Locked tracks are untouched.
* **Layouts by section.** `setLayoutFrom(t, preset)` splits the layout
  region at `t`, and regions with the same preset merge. `layoutAt` falls
  back sensibly before, between and after regions.
* **Subtitles** (`Subtitles.h`): SRT/WebVTT parsing (BOM, CRLF, cue settings,
  inline tags, entities, broken cues skipped) and writing. Cues go onto the
  subtitle track without overlaps.

**Undo/redo** lives in `ui::ProjectController`, the single editing facade QML
calls:
* Every edit applies to a copy of the document and validates it. Success
  becomes one undo step holding the previous document (a memento, not the
  delta commands of §6). Documents are a few hundred KB, so the history is
  bounded at 200 steps.
* Consecutive edits with the same merge key within 1.5 s collapse into one
  step, so a slider drag is undone in one go.
* Saving is transactional on a worker thread, 400 ms after the last edit.
* Each edit publishes an immutable `shared_ptr<const Project>` snapshot for
  playback and export. It is a full copy, not the structural sharing of §8.
* Not built yet: snapping, overwrite/insert drags (§6), clip speed in the UI
  (§9).
* **Keyframe editing UI** (Effects panel): position, scale, rotation, anchor,
  opacity, and crop at the playhead via `ProjectController::setClipKeyframe`.

**Silence removal** (`editor::detectSilences`) measures RMS in 20 ms windows
over the voice tracks only (microphone media; music is ignored). It finds
pauses longer than the minimum, keeps padding around speech, and feeds
`removeRanges` as one undo step.

**Timeline UI** (`qml/editor/TimelinePanel.qml`, `ClipItem.qml`):
* a ruler you can scrub, with markers and the in/out range
* a lane showing layout sections
* track headers with mute, solo, hide and lock
* clips with thumbnails or waveforms, fades, trim handles and drag-to-move
* detected pauses drawn in red
* zoom with Ctrl/⌘+wheel around the cursor, and auto-scroll while playing

Edits are sent once, when a drag ends.

**Tests:** `EditOpsTest` (13), `SubtitlesTest` (5), project migration and
round trips, and `ProjectControllerUi` (7). The last drive the same
controller methods QML calls, and check the views, merged undo steps and the
saved file.

---

## 1. Time representation

```cpp
lectern::Time      // int64 ticks @ 705,600,000 per second ("flicks")
lectern::Rational  // exact num/den (time bases, speeds, frame rates)
lectern::FrameRate // Rational frames per second, e.g. 30000/1001
lectern::TimeRange // half-open [start, start + duration)
```

* **Why ticks instead of seconds as `double`:** doubles cannot represent
  1/30 s or 1001/30000 s exactly, so repeated edits accumulate error and two
  machines can disagree about which frame a cut lands on. Integer ticks are
  exact, deterministic, and fast to compare.
* **Why 705,600,000:** it is divisible by every common frame duration
  (24, 25, 30, 48, 50, 60, 90, 100, 120 fps and the NTSC `/1001` variants) and
  every common audio rate (8 k … 192 k, including the 44.1 k family). Frame
  *and* sample boundaries are exact integers.
* **Conversions** to stream time bases use 128-bit intermediate
  multiplication with explicit `Rounding` (`Floor`, `Ceil`, `Nearest`,
  `TowardZero`). There are no implicit conversions to or from floating
  point.
* **Frame grid:** video edit positions snap to the project frame grid
  (`FrameRate::frameStart(n)`). Audio-only edits may later use sample
  precision, which ticks already support.

## 2. Model

```
Timeline
├── tracks: [Track]          ordered bottom → top for video, then audio
│     Track { id, kind: Video|Audio|Overlay|Subtitle, name,
│             locked, hidden (video), muted, solo (audio),
│             clips: [Clip] sorted by start, non-overlapping }
├── markers: [Marker { id, time, label, color, kind: User|Pause|Chapter }]
└── layoutTrack: [LayoutRegion]   (see §5)

Clip {
  id, kind: Media|Text|Subtitle|Color, name, enabled, linkGroup?
  range:     TimeRange          (position on the timeline)
  source:    { mediaId, sourceIn: Time }   (Media clips)
  speed:     Rational           (1/1 normal, 2/1 = 2x, 1/2 = slow motion)
  transform: Transform          (animated; see §4)
  crop, opacity, effects: [EffectInstance], color: ColorAdjustments
  audio:     { gainDb, volume (animated), fadeIn, fadeOut, muted }
  text / subtitle payloads for Text/Subtitle clips
}
```

### Source time mapping

```
sourceTime(t) = source.sourceIn + (t − range.start) × speed
sourceDuration = range.duration × speed
```

A clip may never reference media beyond the media's duration, except clips
with no media (text, color), which are unbounded.

### Invariants (checked by `Timeline::validate()` and in debug builds after every command)

1. Clips on a track are sorted by `range.start` and do not overlap.
2. `range.duration > 0`; `speed > 0` (reverse playback reserved for later).
3. Every `Media` clip references an existing `MediaSource`.
4. IDs are unique across the project.
5. A clip's track kind is compatible with its media (video media on
   video/overlay tracks, audio media on audio tracks).

## 3. Recording import

A recording becomes one `MediaSource` per track file and one clip per track,
placed at the **file's first timestamp** (each track's offset from session
start, which preserves sync), on dedicated tracks:

```
V3  Phone camera      (Phase 1C)
V2  Camera            clip @ +0.083 s
V1  Screen            clip @ 0
A2  System audio      clip @ 0
A1  Microphone        clip @ +0.012 s
```

All clips from one recording share a `linkGroup`. Move, split and ripple
operate on the whole link group by default (the "Linked selection" toggle),
so a cut on the screen also cuts the camera and both audio tracks. Pause
intervals become `Marker{kind: Pause}`.

## 4. Animation (keyframes)

```cpp
template <class T> struct Animated {
    T base;                                // used when keys is empty
    std::vector<Keyframe<T>> keys;         // sorted by clip-local time
};
struct Keyframe<T> { Time time; T value; Interpolation interp; BezierHandles handles; };
enum class Interpolation { Hold, Linear, EaseIn, EaseOut, EaseInOut, Bezier };
```

* Key times are **clip-local** (relative to `range.start`), so moving a clip
  moves its animation. Trimming the head shifts keys and inserts an
  interpolated key at the new start, so the visual result does not change.
* Easing presets are cubic Béziers (`EaseIn` = (0.42, 0, 1, 1), `EaseOut`
  = (0, 0, 0.58, 1), `EaseInOut` = (0.42, 0, 0.58, 1)), evaluated by solving
  x(u) with Newton–Raphson plus bisection fallback. User-defined Bézier
  curves (`Interpolation::Bezier` + `handles`) reuse the same solver, which
  is why the format already stores handles.
* Keyframeable now: position, scale, rotation, opacity, crop, volume. Effect
  parameters use the same `Animated<T>`.
* Splitting a clip partitions its keys. Both halves receive an interpolated
  key at the cut, so the animation stays continuous.

## 5. Layout system

Screen + face-camera layouts are the product's core editing feature, so they
get first-class modelling rather than being ad-hoc per-clip transforms.

* A **layout track** holds `LayoutRegion { range, preset, params }`
  covering any timeline range. A preset defines target rectangles, shape,
  border, shadow for the *roles* `screen` and `camera` (e.g. Screen only,
  Camera only, PiP bottom-right circle, Side-by-side 60/40).
* At evaluation time, each role's clip receives the active region's
  transform. A clip's own keyframes, if any, are applied on top as relative
  offsets.
* Boundaries between regions animate automatically (default 400 ms
  EaseInOut, configurable per boundary), which produces "camera bottom-right
  → fullscreen → back" with no manual keyframing.
* Selecting a range and choosing a layout splits or merges regions in a
  single undoable command.

## 6. Editing commands (Phase 1D)

```cpp
class ICommand {
public:
    virtual ~ICommand() = default;
    virtual Status execute(EditContext&) = 0;     // apply; may fail with no side effects
    virtual void   undo(EditContext&) = 0;        // must not fail
    virtual bool   mergeWith(const ICommand&) { return false; } // drag coalescing
    virtual std::string_view name() const = 0;
};
```

* `UndoStack` holds commands, supports macro/transaction grouping
  (`beginMacro("Ripple delete")`), a memory/count bound (default 1,000
  commands), and a clean index (drives "unsaved changes").
* Commands store **minimal deltas** (ids + before/after values), never whole
  timeline snapshots.
* Planned commands: `SplitClip`, `TrimClip(head/tail, ripple?)`,
  `MoveClips(delta, track, mode: Overwrite|Insert)`, `DeleteClips(ripple?)`,
  `DuplicateClips`, `PasteClips`, `SetClipProperty<T>` (transform, speed,
  enabled…), `SetKeyframe`, `AddEffect`/`RemoveEffect`/`ReorderEffect`,
  `SetLayoutRange`, `AddMarker`, `TrackFlags`.
* **Overwrite vs insert:** a plain drag overwrites (clips underneath are
  trimmed or split). Shift-drag inserts (ripples later clips). Ripple delete
  closes gaps on the affected tracks and on linked tracks.
* **Snapping:** `SnapIndex` holds the sorted edges of every clip, marker and
  the playhead. A query `snap(t, tolerance)` runs in O(log n). Tolerance is
  given in ticks by the UI from pixel distance and zoom.
* **Selection** is a C++ `SelectionModel`. QML reads it, never owns it.

## 7. Evaluation (render and audio queries)

* `Timeline::activeClips(Time t)`: for each visible video track (bottom →
  top), binary search the sorted clip vector for the clip containing `t`,
  O(tracks · log clips). It returns source time, evaluated transform,
  effects and opacity, which is the compositor's input.
* `Timeline::audioSegments(TimeRange)`: the clips intersecting the range with
  source ranges and gain envelopes (volume keys × fades × mute/solo), which
  is the mixer's input.
* Evaluation is pure: the same timeline and time always produce the same
  result, so it is safe on worker threads against an immutable snapshot (see
  §8).

## 8. Concurrency

* The UI thread owns the mutable `Timeline` and executes commands.
* After each command, an **immutable snapshot** (`std::shared_ptr<const
  TimelineSnapshot>`) is published atomically. Playback, export and
  thumbnail workers read snapshots without locking and pick up the new one
  at the next frame boundary.
* Snapshots share unchanged tracks structurally (copy-on-write per track),
  so dragging one clip does not copy a 2,000-clip timeline.

## 9. Speed

* Video: source frame = `frameAt(sourceTime(t))`, using the nearest frame at
  or before. Frame blending or optical flow are later options.
* Audio: pitch-preserving time stretch (FFmpeg `atempo`, chained outside
  0.5–2.0) for presets 0.25×–4× and custom values. Rendered audio is cached
  per clip and speed.
* Speed is a `Rational`, so 1.5× is exactly 3/2 and timeline math stays
  exact.

## 10. Sync adjustment

* **Manual:** nudge a clip by ±1 ms (audio) or ±1 frame (video) relative to
  its link group (`MoveClips` with link override).
* **Auto Sync:** GCC-PHAT cross-correlation between a reference audio
  (microphone) and another source's audio (phone or camera microphone) over
  a few 10 s windows. The median offset is applied when correlation
  confidence is above a threshold. Used mainly for phone footage (Phase 1C)
  and imported media.

## 11. UI rendering of the timeline

* One custom `TimelineView : QQuickItem` draws clips, thumbnails, waveforms
  and markers with scene-graph nodes (`QSGGeometryNode`, `QSGTextNode`,
  textures). This replaces thousands of QML `Item`s, which do not scale to
  long recordings.
* Clip nodes are rebuilt only for clips whose model changed. The **playhead
  is a separate node** updated each frame without touching clip nodes.
* Thumbnails and waveforms are requested for the visible range at the
  current zoom from background workers (keyframe-based thumbnails;
  multi-resolution min/max peak pyramids for waveforms). An in-memory LRU
  sits on top of disk caches.
* Drag and trim update the model through `mergeWith`-able commands, so
  dragging is a single undo step. Drag and trim never decode media; only
  thumbnails that are already cached are shown during a drag.

## 12. Tests

Unit tests cover time conversions and rounding, frame-grid snapping, model
invariants, serialization round-trips, keyframe interpolation, and every edit
operation in §0: split, trim with ripple and media limits, ripple delete,
remove range(s), move, linked editing, layout sections and subtitles.
Undo/redo is covered by the controller tests.

Still planned: snapping; overwrite/insert moves; property-based tests that
apply random edit sequences and check that undo-all restores the original
serialized timeline byte-for-byte.
