# Lectern — Architecture

> **Lectern** is the working title of this product: a native, cross-platform
> screen recorder and non-linear editor for teachers and course creators.
> The name lives in one CMake variable (`LECTERN_PRODUCT_NAME`) and the C++
> namespace `lectern`, so it can be renamed mechanically.

This document is the entry point. Subsystem details live in:

| Document | Scope |
|---|---|
| [RECORDING_ENGINE.md](RECORDING_ENGINE.md) | Capture, sync, encoding, crash-safe writing, recovery |
| [TIMELINE_ENGINE.md](TIMELINE_ENGINE.md) | Time representation, edit model, commands, evaluation |
| [RENDERING_PIPELINE.md](RENDERING_PIPELINE.md) | Decode → compositor → preview / export |
| [PHONE_CAMERA_PROTOCOL.md](PHONE_CAMERA_PROTOCOL.md) | Phone-as-camera discovery, pairing, transport, clock sync |
| [PROJECT_FORMAT.md](PROJECT_FORMAT.md) | On-disk project layout, schema, autosave, relinking |
| [PERFORMANCE.md](PERFORMANCE.md) | Budgets, hot paths, threading, profiling playbook |

---

## 1. Principles

1. **Performance is the first requirement.** Every design choice is judged by
   copies, allocations, context switches and GPU round-trips on the hot path.
2. **Originals are never modified.** Recording writes each source once;
   editing only changes project metadata.
3. **C++ owns state; QML presents it.** The authoritative project, timeline
   and recording state live in Qt-free C++ libraries. QML binds to thin
   view-model adapters and never holds business state.
4. **Platform code stays behind interfaces.** Editor and engine logic never
   include OS headers. Each OS implements a small set of backend interfaces.
5. **Every long operation is asynchronous, bounded, cancellable and reports
   progress.** No unbounded queues, no unbounded caches, no blocking on the UI
   thread.
6. **Measure, then optimize.** The engine exports counters (queue depths,
   drops, encode latency, write rate). Optimization work starts from profiles,
   not intuition.
7. **Deterministic time.** Timeline math uses integer ticks, never floating
   point seconds (see §6).

## 2. Technology decisions

| Area | Decision | Rejected alternatives & why |
|---|---|---|
| Core language | **C++23** (`std::expected`, ranges, `std::format`, `std::jthread`) | C++17 lacks `expected`/`format`; Rust would cut us off from Qt/FFmpeg ergonomics |
| Apple glue | **Objective-C++ (ARC)** in `src/platform/macos` only | Swift interop with C++ is still awkward for callback-heavy media code |
| UI | **Qt 6 Quick / QML** (scene graph on Qt RHI: Metal, D3D11/12, Vulkan) | Electron/Chromium (memory, startup, no zero-copy video); Qt Widgets (CPU raster, poor for a GPU-heavy editor) |
| Media | **FFmpeg libraries** (`libavformat`, `libavcodec`, `libavfilter`, `libswscale`, `libswresample`) through RAII wrappers | Spawning the `ffmpeg` CLI (no frame-level control, no zero-copy, poor error handling); GStreamer (heavier runtime, weaker Windows story) |
| Hardware codecs | FFmpeg hardware encoders/decoders (VideoToolbox, NVENC/NVDEC, QSV, AMF, Media Foundation, VA-API), **always with a software fallback** | Calling each vendor SDK directly (multiplies maintenance) — still possible behind `IVideoEncoder` if FFmpeg falls short |
| GPU compositor | **Qt RHI** (`QRhi`) so preview textures are shared with the Qt Quick scene graph without copies | A separate Vulkan/Metal engine would need cross-API texture sharing with Qt |
| Serialization | **nlohmann/json** for project, manifests and settings | Qt JSON would pull Qt into the Qt-free core; binary formats hurt debuggability and diffability |
| Tests | **GoogleTest + CTest** | — |
| Build | **CMake ≥ 3.25 + Ninja**, CMake presets | — |
| Dependencies | Homebrew (macOS dev), vcpkg manifest (Windows/Linux CI), Qt online installer/aqtinstall for Qt | Vendoring large third-party trees |

## 3. Module map

```
                    ┌───────────────────────────────────────────┐
                    │ app/ (executables)   tools/ (CLI)         │
                    └───────────────┬───────────────────────────┘
                                    │
                    ┌───────────────▼───────────────┐
                    │ ui/  (Qt Quick, view models)  │  ← only layer that includes QtQuick
                    └───────────────┬───────────────┘
                                    │
                    ┌───────────────▼───────────────┐
                    │ editor/ (QtGui, no QtQuick):  │
                    │  render plan, compositor,     │
                    │  mixer, playback, export      │
                    └───────────────┬───────────────┘
                                    │
                    ┌───────────────▼───────────────┐
                    │ services/ (use-case glue:     │
                    │  recording→project, recovery) │
                    └──┬──────────────┬─────────────┘
                       │              │
        ┌──────────────▼───┐   ┌──────▼──────────────┐   ┌────────────────────┐
        │ capture/         │   │ project/            │   │ platform/<os>/     │
        │ recording engine │   │ project model, I/O  │   │ implements capture │
        └──┬───────┬───────┘   └──────┬──────────────┘   │ interfaces (SCK,   │
           │       │                  │                  │ AVFoundation, ...) │
     ┌─────▼──┐ ┌──▼─────┐     ┌──────▼──────┐           └─────────┬──────────┘
     │ audio/ │ │ media/ │     │ timeline/   │                     │
     │ DSP,   │ │ FFmpeg │     │ edit model  │      (depends on capture, media)
     │ rings  │ │ RAII   │     └──────┬──────┘
     └────┬───┘ └───┬────┘            │
          └────┬────┴─────────────────┘
          ┌────▼────┐
          │ core/   │  time, Result/Error, logging, ids, queues, clocks, fs
          └─────────┘
```

Rules (enforced by CMake target dependencies):

* `core` depends on nothing but the C++ standard library and nlohmann/json.
* `media` wraps FFmpeg; it is the **only** module that includes FFmpeg headers
  in its implementation files, though some public headers expose `AVFrame`
  through RAII types because `AVFrame` is the engine's frame currency
  (it already models CPU planes, GPU surfaces, and reference counting).
* `capture` (the recording engine) depends on `core`, `media`, `audio`. It
  does **not** depend on `project`/`timeline`, so recording and editing are
  independently testable.
* `timeline` and `project` do not depend on `capture`.
* `services` is the only place where recording and project concepts meet.
* `ui` is the only module that links Qt Quick. `core`, `media`, `audio`,
  `capture`, `timeline`, `project`, `services` are **Qt-free**, which lets the
  same engine run inside the GUI, the `lectern-rec` CLI, tests, and a future
  out-of-process export worker.
* `editor` (render plans, compositor, audio mixer, playback engine, silence
  detection, export) depends on `project`, `media`, `audio` and **QtGui only**
  (images, fonts, QPainter). It runs headless, as in `lectern-export` and the
  editor tests on the offscreen platform. Its playback takes an
  `audio::IAudioOutput`, which the platform layer provides.
* `platform/<os>` implements interfaces declared in `capture` and `audio`
  (capture backends, permissions, global hotkeys, audio output). Nothing
  outside `platform/` includes OS SDK headers.

## 4. Repository layout

```
CMakeLists.txt  CMakePresets.json  vcpkg.json
cmake/                      build helpers (warnings, sanitizers, FFmpeg finder)
docs/                       this documentation
src/
  core/                     Time, Rational, Result/Error, Log, Uuid, queues, clocks, fs, settings
  media/                    FFmpeg RAII, probe, encoders, muxer, file sink, salvage remux, hw caps
  audio/                    SPSC audio ring, level meter, timestamp smoothing, drift control
  capture/                  recording engine: sources, session, writers, manifest, recovery
    synthetic/              deterministic test-pattern & tone sources (tests, CI, demos)
  timeline/                 timeline/track/clip model, keyframes, time mapping
  project/                  project model, JSON schema, atomic store
  services/                 recording → project import, recovery orchestration
  render/                   (Phase 1E) GPU compositor
  network/                  (Phase 1C) discovery, pairing, transport
  mobile/                   (Phase 1C) desktop-side phone camera management
  platform/
    macos/                  ScreenCaptureKit, AVFoundation, CoreAudio, permissions
    windows/                (WGC, Media Foundation, WASAPI) — stubs, see status table
    linux/                  (PipeWire portal, V4L2, PipeWire audio) — stubs
  ui/                       Qt Quick view models + QML module
  app/                      main executable
tools/                      lectern-rec (headless recorder), lectern-probe
tests/unit, tests/integration
mobile/android, mobile/ios  companion apps (Phase 1C)
```

## 5. Process and threading model

Single desktop process for Phase 1, with clear seams for isolation later:

| Thread / context | Owner | Rules |
|---|---|---|
| UI thread | Qt | Never decodes, encodes, probes, or touches disk synchronously. Receives engine events through queued signals. |
| Capture callbacks | OS (SCK dispatch queue, AVFoundation queue, CoreAudio IOProc) | Do the minimum: timestamp, wrap buffer (zero-copy), `try_push` into a bounded queue. Audio IOProc: no locks, no allocation, no logging. |
| Audio pump (1 per audio source) | engine | Drains the lock-free ring, meters, drift-corrects, encodes. |
| Video encode (1 per video track) | engine | Paces to constant frame rate, encodes. |
| Mux / I/O (1 per track) | engine | Writes packets, flushes, isolates disk stalls from encoders. |
| Monitor | engine | 4 Hz stats, disk-space checks, manifest checkpoints. |
| Background workers | `core` thread pool | Probing, thumbnails, waveforms, proxies (later phases). |

Engine → UI communication: engines expose **snapshots** (cheap, lock-free or
short-lock copies) that the UI polls at display rate, plus **events** (state
changes, errors) posted to the UI thread. The UI never receives one signal per
frame or per audio buffer.

Future isolation seams (documented, not yet implemented): export runs in a
worker process (`lectern-export`) so a codec crash cannot take down an
unsaved edit session; the recording engine can move into a helper process
because it is already Qt-free and headless (proven by `lectern-rec`).

## 6. Time

* **Timeline/session time** is `lectern::Time`: a signed 64-bit count of
  *ticks* at **705,600,000 ticks/second** ("flicks"). Every common video
  frame duration (23.976, 24, 25, 29.97, 30, 48, 50, 59.94, 60, 120 fps) and
  every common audio sample period (8 kHz … 192 kHz, 44.1 kHz family
  included) is an exact integer number of ticks. Range: ±414 years.
* **Stream time** keeps each file's own time base (`Rational`), converted with
  128-bit intermediate math and explicit rounding modes.
* **Host time** is nanoseconds of the platform monotonic clock that capture
  APIs use for timestamps: `mach_absolute_time` (macOS), QPC (Windows),
  `CLOCK_MONOTONIC` (Linux). Recording maps host time to session time.
* Floating-point seconds appear only at UI boundaries.

## 7. Errors and logging

* Fallible functions return `lectern::Result<T>` (= `std::expected<T, Error>`)
  or `Status` (= `Result<void>`). `Error` carries a category code, message,
  native code (errno / `OSStatus` / `HRESULT` / `AVERROR`) and a context chain.
* Exceptions do not cross module boundaries. Third-party exceptions
  (nlohmann/json, `std::filesystem`) are caught at the call site and converted.
* Logging is asynchronous with a bounded queue. If the queue is full, messages
  are dropped and counted rather than blocking the caller. Real-time audio
  callbacks never log; they increment atomic counters that the monitor thread
  reports.

## 8. Platform abstraction

| Interface (capture/) | macOS | Windows | Linux |
|---|---|---|---|
| `IScreenCaptureBackend` (displays, windows, apps) | ScreenCaptureKit ✅ | Windows.Graphics.Capture ⏳ | xdg-desktop-portal ScreenCast + PipeWire ⏳ |
| `ICameraCaptureBackend` | AVFoundation ✅ | Media Foundation ⏳ | V4L2 / PipeWire camera ⏳ |
| `IAudioCaptureBackend` – microphone | Core Audio HAL ✅ | WASAPI ⏳ | PipeWire ⏳ |
| `IAudioCaptureBackend` – system audio | ScreenCaptureKit audio ✅ (Core Audio taps later) | WASAPI loopback ⏳ | PipeWire monitor ⏳ |
| `IVideoEncoder` (hardware) | VideoToolbox via FFmpeg ✅ | NVENC/QSV/AMF/MF via FFmpeg ✅ (selection logic; unverified on hardware) | VA-API/NVENC via FFmpeg ✅ (unverified) |
| `IPermissionService` | TCC (screen, camera, mic) ✅ | — (WGC picker / privacy settings) ⏳ | portal consent ⏳ |

✅ implemented and verified on macOS · ⏳ interface ready, backend not yet
implemented (the synthetic backend is used so the engine is still testable).

## 9. Hardware acceleration

At startup a background task probes encoders by **opening** them with a small
test configuration (an encoder that is merely compiled into FFmpeg can still
fail at runtime because the GPU or driver is missing). Results are cached for
the session and shown in Advanced Settings. Selection order:

* macOS: `h264_videotoolbox` / `hevc_videotoolbox` → `libx264` / `libx265`
* Windows: `h264_nvenc` → `h264_qsv` → `h264_amf` → `h264_mf` → `libx264`
* Linux: `h264_vaapi` → `h264_nvenc` → `h264_qsv` → `libx264`

Software fallback is always available. Nothing assumes a particular GPU exists.

## 10. Security

* The desktop never exposes an unauthenticated listener. The phone-camera
  service listens only while pairing or while a paired device is expected,
  requires mutual TLS with pinned certificates, and requires explicit user
  approval for new devices. See [PHONE_CAMERA_PROTOCOL.md](PHONE_CAMERA_PROTOCOL.md).
* Project and manifest parsing treats input as untrusted (size limits, schema
  validation, no path traversal outside the project folder for media
  references unless the user explicitly linked an external file).

## 11. Licensing notes (must be resolved before distribution)

* **Qt** is used under LGPLv3 (dynamic linking) or a commercial license.
* **FFmpeg** must be built **LGPL** for a proprietary product: no
  `--enable-gpl`, so no libx264/libx265. The Homebrew build used for
  development is GPL. Distribution builds should use hardware encoders first
  and a licensed software fallback such as OpenH264 (Cisco binary) or a
  commercial x264 license. The encoder selection already treats the software
  encoder as configurable.
* H.264/HEVC/AAC carry patent-pool obligations that depend on the
  distribution model.

## 12. Phase status

| Phase | Scope | Status |
|---|---|---|
| 1A | Build system, core, media wrappers, models, Qt shell | Done |
| 1B | Recording engine, backends, recovery, recording UI, CLI | Done and verified on macOS; Windows backends implemented and compile-checked, not yet run; Linux pending |
| 1C | Phone camera | Protocol designed; not implemented |
| 1D | Editor core: edit operations, undo/redo, interactive timeline, selection | Done (TIMELINE_ENGINE.md §0) |
| 1E | Compositor (CPU, shared by preview and export), playback with sound | Done; GPU (QRhi) compositor planned (RENDERING_PIPELINE.md §0) |
| 1F | Creator tools: text, subtitles, overlays, effects, color, audio mixer, music | Done |
| 1G | Export (MP4, hardware H.264 + AAC), `lectern-export` | Done |
| 1H | Profiling and optimization | In progress (PERFORMANCE.md §10) |
