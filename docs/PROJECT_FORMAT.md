# Project Format

A project is a **folder** containing one JSON document (`project.json`) plus
media and disposable caches. Editing changes only `project.json`. Recorded
and imported media files are never rewritten.

Code: `src/project/` (model, JSON (de)serialization, atomic store),
`src/timeline/` (timeline types), `src/core/FileSystem.*` (atomic writes,
locks).

---

## 1. Folder layout

```
Lesson 01.lectern/
├── project.json              authoritative project document
├── project.json.bak          previous successfully saved version
├── .lock                     exclusive lock held by the editor that has the project open
├── media/                    recorded media (owned by the project)
│   ├── screen/   screen-<session>.mkv
│   ├── camera/   camera-<session>.mkv
│   ├── phone/    phone-<session>-hq.mkv, phone-<session>-preview.mkv
│   ├── audio/    microphone-<session>.mkv, system-audio-<session>.mkv
│   └── imported/ (only if the user chose "copy into project")
├── recordings/<session>/session.json   recording manifests (see RECORDING_ENGINE.md §8)
├── autosave/  project-YYYYMMDD-HHMMSS.json   (rotating, newest 10)
├── proxies/   <fingerprint>-<variant>.mkv    ┐
├── thumbnails/<fingerprint>/...              │ disposable: safe to delete,
├── waveforms/ <fingerprint>.peaks            │ rebuilt on demand
└── cache/                                    ┘
```

* Imported media is **referenced in place** by default (no copy, no
  transcode). Copying into `media/imported/` is an explicit option.
* Media paths inside the project folder are stored **relative**, so the
  whole folder can be moved or synced. External media use absolute paths
  plus a fingerprint for relinking.

## 2. `project.json`

```jsonc
{
  "format": "lectern.project",
  "formatVersion": 2,
  "timebase": 705600000,                 // ticks per second for every time value below
  "id": "0199b2a4-6f3e-7c41-9a5e-2b0d7c9e1f00",   // UUIDv7
  "title": "Lesson 01 — Variables",
  "description": "",
  "createdAt": "2026-10-04T12:30:05Z",
  "modifiedAt": "2026-10-04T12:58:41Z",
  "app": { "name": "Lectern", "version": "0.1.0" },

  "canvas": {
    "width": 1920, "height": 1080,
    "frameRate": { "num": 30, "den": 1 },
    "aspect": "16:9",                      // 16:9 | 9:16 | 1:1 | 4:5 | custom
    "background": { "type": "color", "color": "#0E0F13" }
  },

  "style": {                             // project-wide look (v2); lengths are fractions of the canvas height
    "backgroundColor2": "#2C5364",       // gradient end ("" = solid background)
    "screenPadding": 0.05, "screenRadius": 0.015, "screenShadow": 0.6,
    "cameraShape": "circle",             // rect | rounded | circle (circle layouts force circle)
    "cameraBorder": 0.004, "cameraBorderColor": "#FFFFFF", "cameraMirror": false,
    "subtitleSize": 0.045, "subtitleColor": "#FFFFFF",
    "subtitleBackground": "#B3000000",   // "" = no box
    "subtitlePosition": 0.88
  },

  "media": [{
    "id": "…", "kind": "video", "role": "screen",   // screen|camera|phone|microphone|systemAudio|imported
    "name": "Screen", "path": "media/screen/screen-0199b2a4.mkv",
    "fingerprint": { "size": 734003200, "partialHash": "fnv1a64:9b1f…" },
    "info": {
      "container": "matroska", "duration": 1270080000000, "start": 0,
      "video": { "codec": "h264", "width": 1920, "height": 1080,
                 "frameRate": { "num": 30, "den": 1 }, "pixelFormat": "yuv420p",
                 "colorSpace": "bt709", "colorRange": "tv", "rotation": 0, "vfr": false },
      "audio": null
    },
    "recording": { "sessionId": "…", "trackId": "screen" },
    "proxy": null,
    "syncOffset": 0
  }],

  "timeline": {
    "tracks": [{
      "id": "…", "kind": "video", "name": "Screen",          // video|audio|overlay|subtitle
      "locked": false, "hidden": false, "muted": false, "solo": false,
      "clips": [{
        "id": "…", "kind": "media", "name": "Screen", "enabled": true, "linkGroup": "…",
        "start": 0, "duration": 1270080000000,
        "mediaId": "…", "sourceIn": 0, "speed": { "num": 1, "den": 1 },
        "transform": {
          "fit": "fit",                                     // fit|fill|stretch|none
          "position": { "value": [0.5, 0.5] },              // anchor in normalized canvas coords
          "scale":    { "value": [1.0, 1.0] },
          "rotation": { "value": 0.0 },                     // degrees
          "anchor": [0.5, 0.5], "flipH": false, "flipV": false
        },
        "crop":    { "value": [0, 0, 0, 0] },              // left, top, right, bottom (fractions)
        "opacity": { "value": 1.0,
                     "keys": [{ "t": 0, "v": 0.0, "interp": "easeOut" },
                              { "t": 352800000, "v": 1.0, "interp": "linear" }] },
        "audio":   { "gainDb": 0.0, "volume": { "value": 1.0 }, "fadeIn": 0, "fadeOut": 0, "muted": false },
        "color":   { "exposure": { "value": 0.0 }, "contrast": { "value": 0.0 } /* … */ },
        "effects": [{ "id": "…", "type": "lectern.blur.gaussian", "version": 1, "enabled": true,
                      "params": { "radius": { "value": 8.0 } } }]
      }]
    }],
    "markers": [{ "id": "…", "time": 211680000000, "label": "Paused", "kind": "pause", "color": "#F5A524" }],
    "layout": [{ "id": "…", "start": 0, "duration": 2116800000, "preset": "screen.only" }]
  },

  "recordings": [{ "sessionId": "…", "manifest": "recordings/…/session.json",
                   "startedAt": "…", "duration": 1270080000000, "state": "completed" }],

  "export": { "container": "mp4", "videoCodec": "h264", "width": 1920, "height": 1080,
              "frameRate": null, "quality": "high", "audioCodec": "aac", "audioBitrate": 192000 }
}
```

### Encoding rules

* **Time values** (`start`, `duration`, `sourceIn`, `time`, `t`, `fadeIn`, …)
  are integers in `timebase` ticks (705,600,000/s). Speeds and frame rates
  are rationals `{num, den}`. JSON numbers above 2⁵³ are never produced:
  414 years fit, but ~104 days is the limit for exact IEEE-754 doubles in
  third-party tools, which is far beyond any project.
* **Animated values** are `{ "value": V, "keys": [ { "t", "v", "interp",
  "in"?, "out"? } ] }`. `keys` is omitted when empty. Key `t` is clip-local.
  `in`/`out` hold Bézier handles for `interp: "bezier"`.
* **IDs** are UUIDv7 strings (time-ordered, globally unique).
* **Colors** are `#RRGGBB`, or `#AARRGGBB` with alpha first (Qt's order,
  e.g. `#B3000000` is 70 % black). Note this is not CSS's `#RRGGBBAA`.
* **Tracks** carry `gainDb` (track volume, v2) next to `muted`/`solo`/`locked`/`hidden`.
* **Clip transforms of screen and camera clips** are adjustments relative to
  the layout preset's slot (v2): position (0.5, 0.5) and scale 1 mean "exactly
  the slot". Text and overlay clips are placed freely (position = center in
  canvas fractions, scale = width as a fraction of the canvas width).
* Unknown keys are ignored on load with a logged warning, except the
  reserved `extensions` object at any level, which is preserved verbatim
  (for plug-ins and forward compatibility).

## 3. Versioning and migration

* `formatVersion` is an integer. The loader accepts `1 … current` and runs
  pure JSON→JSON migrations before typed parsing. Implemented:
  **v1 → v2.** Version 1 stored an absolute picture-in-picture placement on
  camera clips (position 0.84/0.80, scale 0.28). That exact legacy default
  becomes the identity; any other placement is kept. Missing `style` and
  `gainDb` take their defaults. Tested by
  `ProjectJson.MigratesV1CameraPlacementToLayoutRelativeTransform`.
* A file with a **newer** `formatVersion` is refused with "This project was
  created by a newer version of Lectern". It is never partially loaded,
  because a later save would drop data.
* Validation after parsing enforces the timeline invariants
  (TIMELINE_ENGINE.md §2) and size limits (e.g. ≤ 100k clips, strings ≤ 64
  KiB). Invalid files produce precise error paths
  (`timeline.tracks[2].clips[5].duration: must be > 0`).

## 4. Saving

**Transactional save** (never overwrite the only valid copy):

1. Serialize on the UI thread (from an immutable snapshot); write on a
   background thread.
2. Write `project.json.tmp`, then `fsync`.
3. Refresh `project.json.bak` from the current `project.json` (hard link
   swap, or copy where links are unsupported).
4. `rename(project.json.tmp → project.json)` (atomic replace on POSIX;
   `ReplaceFileW` on Windows), then `fsync` the directory.

At every instant either the old or the new `project.json` exists and is
complete.

**Autosave:** when the project is dirty, 2 s after the last edit and at most
every 60 s, write `autosave/project-<timestamp>.json` (same atomic
procedure; rotation keeps the newest 10). *Status:* the writer and rotation
(`ProjectStore::writeAutosave`) are implemented and tested; the scheduler is
wired up with the editing commands in Phase 1D.

**Crash recovery:** *(Phase 1D, with editing)* the editor holds an exclusive OS lock on `.lock` while a
project is open. On open, if the lock is free but an autosave is newer than
`project.json`, offer "Restore unsaved changes from <time>?". If
`project.json` fails to parse, fall back to `.bak`, then to the newest
autosave, and explain to the user what was restored.

## 5. Media references and relinking

* Each `MediaSource` stores `path` (relative when inside the project), a
  **fingerprint** (`size` + FNV-1a-64 of the first and last 1 MiB), and probed
  `info`.
* On open, missing media are flagged (clips render as "Media offline"). Relink
  searches the original directory, then the project folder, then a folder the
  user picks, and accepts candidates whose fingerprint matches. Fingerprint
  mismatches require confirmation.
* Phone HQ relink (PHONE_CAMERA_PROTOCOL.md §10) switches `path` to the HQ
  file and stores the previous file as `proxy`.

## 6. Disposable caches

`proxies/`, `thumbnails/`, `waveforms/`, `cache/` are keyed by media
fingerprint, never by media id, so duplicated media share caches and
relinked media keep theirs. Deleting these folders is always safe. Size
limits are enforced by `CacheManager` (RENDERING_PIPELINE.md §9).
