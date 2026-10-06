# Lectern

A native, high-performance screen recorder and editor for teachers and course
creators. Screen, cameras, microphone and system audio are recorded as
**separate, synchronized tracks**, so layouts can change after recording
without re-recording anything.

*Lectern is the working title (one CMake variable: `LECTERN_PRODUCT_NAME`).*

- C++23 engine (Qt-free), Qt 6 Quick UI, FFmpeg libraries (no CLI spawning)
- Hardware encoding with software fallback; zero-copy capture → encode on macOS
- Crash-safe recording with automatic recovery
- Non-destructive editor: cut, layouts, text, subtitles, effects, color, audio mix, export
- One compositor and one audio mixer for preview and export, so what you see and hear is what you export

## Status

| Phase | Scope | State |
|---|---|---|
| 1A | Architecture docs, CMake, core library, FFmpeg wrappers, project/timeline models, Qt shell | ✅ Done |
| 1B | Recording engine, capture backends, crash recovery, recording UI, `lectern-rec` CLI | ✅ Verified on macOS · Windows backends written and compile-checked, **not yet run on Windows** · Linux pending (synthetic fallback) |
| 1C | Phone as wireless camera | Designed ([protocol](docs/PHONE_CAMERA_PROTOCOL.md)) |
| 1D | Editing engine: split, trim, ripple delete, remove section/pauses, layouts by section, undo/redo, markers | ✅ Done |
| 1E | Compositor (CPU, shared by preview and export) and real-time playback with sound | ✅ Done · GPU (QRhi) compositor planned |
| 1F | Creator tools: text presets, subtitles (SRT/VTT), overlays, effects, color, audio mixer, music | ✅ Done |
| 1G | MP4 export (H.264 + AAC, hardware encoder), `lectern-export` CLI | ✅ Done |
| 1H | Profiling and GPU compositor | In progress ([PERFORMANCE.md](docs/PERFORMANCE.md)) |

Start with [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). **System requirements**
(macOS / Windows / Linux, RAM, permissions): [docs/SYSTEM_REQUIREMENTS.md](docs/SYSTEM_REQUIREMENTS.md).
The recording engine is
specified in [docs/RECORDING_ENGINE.md](docs/RECORDING_ENGINE.md), the editor in
[docs/TIMELINE_ENGINE.md](docs/TIMELINE_ENGINE.md) and
[docs/RENDERING_PIPELINE.md](docs/RENDERING_PIPELINE.md); each lists how it was verified.

## Build (macOS)

```bash
brew install cmake ninja pkgconf ffmpeg qtbase qtdeclarative qtsvg googletest nlohmann-json
cmake --preset dev          # Debug; also: release, engine (no Qt), asan, tsan
cmake --build --preset dev
ctest --preset dev          # 155 unit + integration tests, ~30 s
```

The project path may contain spaces. Development builds link Homebrew
libraries; distribution builds should set `-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0`
with dependencies built for that target (see the licensing notes in
[ARCHITECTURE.md §11](docs/ARCHITECTURE.md)).

**Windows (64-bit, Windows 10 1903+ / 11):** see [docs/WINDOWS.md](docs/WINDOWS.md).
Run `scripts/build-windows.ps1` or CI workflow **Windows app** for a portable
`Lectern.exe` ZIP. Native backends (WGC, WASAPI, Media Foundation) compile with
MSVC; device testing on Windows is still limited.
**Linux:** native capture is not implemented yet; the app runs with synthetic sources.

## Run

```bash
open build/dev/bin/Lectern.app                       # the app
build/dev/bin/Lectern.app/Contents/MacOS/Lectern --synthetic   # no real devices
```

Recording: **⌘⇧R** start/stop (works system-wide) · **⌘⇧P** pause/resume · **Esc** cancel the countdown.
During a recording the main window minimizes and a floating control bar stays on
top (Lectern's own windows are excluded from the capture).

Editing: **Space** play/pause · **S** split · **⌫** delete (recording segments close the gap) ·
**M** marker · **←/→** one frame (**⇧** one second) · **I/O** mark a section to remove ·
**⌘Z/⌘⇧Z** undo/redo · **⌘E** export · **Esc** deselect. Drag clip edges to trim,
drag text/overlay/music clips to move them, drag the selected layer on the preview
to place it. The left rail opens Setup, Layout, Cut, Effects, Overlay, Style (text),
Subtitles, Audio and Adjust (color).

On first use macOS asks for Screen Recording, Camera and Microphone access.
Screen Recording changes take effect after relaunching the app.

### Command-line tools

```bash
build/dev/bin/lectern-rec --list                                   # devices, permissions, encoders
build/dev/bin/lectern-rec --display main --camera default --mic default --system-audio \
                          --fps 30 --resolution 1080p --stats      # Ctrl+C to stop
build/dev/bin/lectern-rec --synthetic --duration 10 --out /tmp/demo.lectern
build/dev/bin/lectern-rec --recover                                # salvage interrupted recordings
build/dev/bin/lectern-export /tmp/demo.lectern --out demo.mp4 --resolution 1080p   # export an edit
build/dev/bin/lectern-export /tmp/demo.lectern --frame 3.5 --png frame.png         # render one frame
build/dev/bin/lectern-probe <file>                                 # media metadata as JSON
```

Recordings are created as project folders (default `~/Movies/Lectern`):

```
Recording 2026-10-04 14.22.lectern/
  project.json                       non-destructive edit document (format 2)
  media/screen|camera|audio/*.mkv     one file per source, session-time timestamps
  media/imported/                     overlays and music added in the editor
  recordings/<session>/session.json   recording manifest (sync + recovery)
```

## Tests

```bash
ctest --preset dev                                   # default suite
ctest --test-dir build/dev -L long                   # one simulated hour (≈1 min)
ctest --test-dir build/dev -L device                 # real devices (needs permissions)
LECTERN_TEST_MICROPHONE=1 ctest --test-dir build/dev -L device   # also records the mic
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan
```

Highlights (all passing, details in [PERFORMANCE.md §9–10](docs/PERFORMANCE.md)):

- End-to-end sync with drifting audio clocks, jitter, pauses, device gaps and late starts — worst A/V offset 2.1 ms (17 s, 500 ppm drift) and 0.27 ms after one simulated hour (250 ppm drift)
- Crash recovery from a real `SIGKILL` mid-recording; disk-full auto-stop; disk stalls never write undecodable data
- Editing engine: split/trim/ripple/remove-range/silence removal keep every track, marker and layout section in sync (property checks on picture and sample-exact audio)
- Playback: the playhead equals what the speaker has played (A/V sync verified sample-exactly, also under load); seeks never play stale audio
- Export: picture and sound follow cuts frame- and sample-accurately; MP4 index at the front; hardware H.264 at ~2× real time for a styled 1080p30 composition
- Clean under ThreadSanitizer and Address/UndefinedBehavior sanitizers (all 153 engine and UI-controller tests)
- Real capture on an M1 MacBook Air: 1080p60 screen + camera, 0 dropped frames, ~14 % of one core, 95 MB peak memory

## Repository layout

```
docs/          architecture and subsystem designs
cmake/         build helpers (warnings, sanitizers, FFmpeg finder)
src/core       time (integer "flicks"), errors, logging, ids, queues, lock-free rings, clocks, fs, settings
src/media      FFmpeg RAII, encoders (hw + sw), muxer, AVIO sink, readers (video/audio by time), waveforms, probe, salvage
src/audio      level meter, timestamp smoothing, drift control, splice fades, audio output interface
src/capture    recording engine: sources, session clock, CFR pacer, track writers, manifest, recovery
src/timeline   timeline model, keyframes, edit operations, subtitles (SRT/VTT), JSON
src/project    project model, versioned JSON with migrations, transactional store
src/editor     render plans, layouts, compositor, audio mixer, playback engine, silence detection, export (QtGui)
src/services   recording → project import, recording setup
src/platform   macOS (ScreenCaptureKit, AVFoundation, Core Audio) and Windows (WGC, WASAPI, Media Foundation) backends
src/ui, src/app  Qt Quick application (view-models + QML panels)
tools/         lectern-rec, lectern-export, lectern-probe
tests/         unit, integration, editor and UI-controller tests
mobile/        companion apps (Phase 1C)
```

## Known limitations

- **Windows:** capture, audio output and hotkeys are implemented and compile against
  the Windows SDK headers (MinGW-w64), but have not been built with MSVC nor run on
  Windows yet. Linux capture is not implemented (synthetic sources are used).
- The Core Audio microphone path is implemented but has not been recorded on
  hardware yet (permission was not granted on the development machine).
- The compositor runs on the CPU (QPainter). Preview is smooth at preview size;
  export runs at ~2× real time for 1080p30. A GPU (QRhi) compositor is the next
  performance step.
- Retimed clips (speed ≠ 1×) play without sound; clip speed is not exposed in the UI yet.
  No transitions between segments, no keyframe editing UI (effects are per clip).
- FFmpeg from Homebrew is GPL-licensed; a commercial release needs an LGPL build
  ([ARCHITECTURE.md §11](docs/ARCHITECTURE.md)).
