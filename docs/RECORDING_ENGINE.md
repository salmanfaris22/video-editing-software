# Recording Engine (Phase 1B)

The recording engine captures several independent sources (screen, cameras,
microphone, system audio, and later phones) into **separate files that
share one session timeline**. It must survive hour-long sessions, keep memory
bounded, never stall the UI, stay in sync to a few milliseconds, and leave
recoverable media behind if the process dies.

Code: `src/capture/` (engine, Qt-free), `src/audio/` (real-time audio
primitives), `src/media/` (encoders, muxer, file sink, salvage),
`src/platform/<os>/` (backends), `src/capture/synthetic/` (test sources),
`tools/lectern-rec` (headless CLI).

---

## 1. Concepts

| Concept | Meaning |
|---|---|
| **Source** (`IVideoSource`, `IAudioSource`) | A running OS capture: one display/window/app, one camera, one microphone, system audio. Produces timestamped frames/samples in **host time**. |
| **Live source** (`LiveVideoSource`, `LiveAudioSource`) | A started source plus its fan-out: preview slot, level meters, and an optional recording consumer. Lives longer than a recording. |
| **Session** (`RecordingSession`) | One recording. Owns the `SessionClock`, one **track writer** per source, the manifest, and the disk monitor. |
| **Track writer** | Converts one source's host-timed media into one file whose timestamps are **session time**. |
| **Session clock** | Maps host time → session time and handles start, pauses and stop. |
| **Manifest** | `session.json`, an atomically rewritten description of the session used for project creation and crash recovery. |

## 2. Lifecycle

```
            open sources (preview + meters live)
  ┌──────┐  ─────────────────────────────────▶ ┌─────────┐
  │ Idle │                                     │  Armed  │  (UI concept: sources live)
  └──────┘                                     └────┬────┘
                                         start()    │  T0 = host now
                                                    ▼
                              pause()  ┌─────────────────────┐
                    ┌──────────────────│      Recording      │◀──────────┐
                    ▼                  └──────────┬──────────┘           │
              ┌──────────┐   resume()             │ stop() / disk full   │
              │  Paused  │────────────────────────┼──────────────────────┘
              └────┬─────┘                        ▼
                   │ stop()             ┌───────────────────┐
                   └───────────────────▶│     Stopping      │ drain to S, flush, finalize
                                        └───────┬───────────┘
                                                ▼
                                   Completed │ Failed │ Cancelled (files deleted)
```

* Sources are started **before** `start()` while the recording screen is open
  (camera preview, mic meter) and, for the screen, during the 3-2-1 countdown.
  This removes device start-up latency from the recording: when the user hits
  record, every source is already producing data. A source that starts late
  is still handled correctly, because its track simply begins at a later
  session time.
* `start()`, `pause()`, `resume()`, `stop()` read the host clock **inside the
  session-clock lock**, so every later timestamp query sees a linearizable
  boundary (see §4).
* A failing track (device lost, encoder error) does not fail the session.
  The other tracks keep recording, and the failure is recorded in the
  manifest and surfaced in the UI. Only session-wide failures (output volume
  unwritable, disk full) stop everything, and they still finalize every file.

## 3. Clock domains and timestamps

All capture timestamps are converted at the backend boundary into **host
nanoseconds** on the clock the OS media stack already uses:

| OS | Host clock | Native timestamp sources |
|---|---|---|
| macOS | `mach_absolute_time` → ns (`CMClockGetHostTimeClock`) | SCK `CMSampleBuffer` PTS (host clock), AVFoundation PTS converted from `AVCaptureSession.synchronizationClock` with `CMSyncConvertTime`, Core Audio `AudioTimeStamp.mHostTime` |
| Windows | QPC → ns | WGC `SystemRelativeTime`, WASAPI `u64QPCPosition`, Media Foundation sample time (QPC-based) |
| Linux | `CLOCK_MONOTONIC` | PipeWire `spa_meta_header.pts`/`pw_time.now`, V4L2 buffer timestamps (`V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC`) |

`HostClock::nowNs()` reads the same domain, so "now" and capture timestamps
are directly comparable.

**Capture latency compensation.** Backends report a timestamp for the moment
light or sound was captured, not the moment the callback ran:

* Core Audio input: `mHostTime(first sample) − (device latency + stream
  latency + safety offset) / sample rate`. This matters most for Bluetooth
  microphones, which add 100–200 ms. The applied compensation is written to
  the manifest.
* Video: SCK and AVFoundation PTS already describe capture time.
* The editor's manual offset (±ms per media) and Auto Sync (audio
  cross-correlation) fix residual device-specific latency.

## 4. Session time, pauses, and stop

```
host time  ──T0────────P1══════R1────────P2═════R2──────S──▶
session    0 ──────────a        a──────────b       b─────c
```

`sessionTime(h) = h − T0 − Σ(paused durations that ended before h)`. It is
undefined (data is dropped) for `h < T0`, inside a pause `[Pk, Rk)`, and for
`h ≥ S`. Session time is converted to `Time` ticks (705,600,000/s).

* **Video** frames are kept or dropped by their capture timestamp, not their
  arrival time. A frame captured just before `P1` that arrives just after it
  is still recorded.
* **Audio** chunks that straddle a boundary are split **sample-accurately**:
  the boundary host time maps to a fractional input sample index through the
  smoothed device clock (§6). A 5 ms fade-out before each pause/stop splice
  and a 5 ms fade-in after each resume remove clicks. The writer holds a 10 ms
  output delay line so the fade-out can be applied retroactively.
* **Keyframes:** the first video frame after a resume is forced to be an
  IDR frame, so pause points are clean edit/seek points.
* **Stop drain:** after `stop()` at host `S`, writers keep consuming until
  every source has delivered data captured at or after `S`, or a drain
  timeout expires (video 500 ms, audio 300 ms). The video pacer then emits
  slots up to `S` and audio is padded with silence up to `S`, so **all
  tracks end at the same session time**.
* Pause intervals are stored in the manifest and become timeline markers.

## 5. Video path

```
OS callback ─wrap (zero-copy)─▶ LiveVideoSource router ─┬─▶ preview slot (latest frame)
                                                        └─▶ try_push ▶ [bounded input queue, 6]
                                                                            │ encode thread
                                       session-time mapping ◀───────────────┘
                                              ▼
                                       CFR pacer ─▶ encoder ─▶ [packet queue ≤ 64 MB] ─▶ mux thread ─▶ file
```

### 5.1 Zero-copy frames

`AVFrame` is the frame currency. On macOS, SCK and AVFoundation deliver
IOSurface-backed `CVPixelBuffer`s in NV12. The backend wraps each one as an
`AV_PIX_FMT_VIDEOTOOLBOX` hardware frame (retained, not copied) carrying an
`hw_frames_ctx`, and `h264_videotoolbox` encodes it directly. Window server →
encoder involves **no CPU copy**. Scaling to the requested output size is done
by ScreenCaptureKit on the GPU (`SCStreamConfiguration.width/height`).

If the selected encoder is software (`libx264`), the writer downloads the frame
once (`av_hwframe_transfer_data`, a single NV12 copy) into an `AVFrame`.

### 5.2 Constant-frame-rate pacing

Screen capture is naturally variable-rate: ScreenCaptureKit emits nothing
while the screen is static. Cameras slow down in low light. The writer
produces **constant frame rate** output (simplest for editors, players and
the timeline) with `VideoPacer`:

* Output slot `k` covers session time `k·D` (`D` = 1/fps). A frame at session
  time `t` is assigned to slot `round(t/D)`.
* Frames arrive in capture order, so a frame for slot `s` closes every slot
  `< s`: closed slots are emitted with the pending frame, or with a
  **duplicate** of the last emitted frame.
* If no frames arrive, slots are closed when `now > k·D + 100 ms`, which
  produces duplicates for a static screen.
* Two frames mapping to one slot: the newer wins (`decimated`). A frame for an
  already-emitted slot only refreshes the duplicate source (`late`).
* `maxHold` bounds duplication: unlimited for screens (static content is
  normal), 2 s for cameras (longer means the device stalled; the track gets a
  timestamp gap and the stall interval is recorded in the manifest).
* At `start()` the pacer is **seeded** with the newest pre-roll frame, so slot 0
  shows what was on screen at T0 even if the screen is static.
* The pacer holds at most two frame references (pending and last), which
  matters because ScreenCaptureKit only has `queueDepth` surfaces in flight.

### 5.3 Back-pressure policy (never block a capture thread)

| Queue | Bound | When full |
|---|---|---|
| capture → encoder | 6 frames | capture callback drops the **new** frame (`droppedQueueFull`); the pacer duplicates; UI warns "encoder overloaded" |
| encoder → mux | 64 MB video / 8 MB audio | encoder thread blocks up to 2 s, which pushes pressure back to the first queue; if the disk stalls longer, packets are dropped **until the next keyframe** so the file always stays decodable |
| audio RT ring | 2 s | RT callback drops samples and counts `overflowSamples`; the drift controller later inserts silence so sync is preserved |

### 5.4 Encoding defaults

| Track | Codec | Settings |
|---|---|---|
| Screen | H.264 High | CFR at selected fps, GOP 2 s, no B-frames, ABR (below), hardware preferred |
| Camera | H.264 High | Native camera mode ≤ requested size, GOP 2 s, no B-frames |
| Microphone / system audio | **FLAC 24-bit, 48 kHz**, source channel count (1–2) | lossless |

Average bitrates (Mbit/s, "High" quality; "Standard" is ×0.6):

| | 720p | 1080p | 1440p | 2160p |
|---|---|---|---|---|
| Screen 24/30 fps | 5 | 10 | 16 | 35 |
| Screen 60 fps | 8 | 16 | 25 | 55 |
| Camera 30 fps | 6 | 12 | 20 | 40 |

**Why no B-frames:** lower encoder latency and fewer frames (and IOSurfaces)
held inside the encoder, timestamps where PTS equals DTS, and cheaper
scrubbing in the editor. The bitrate cost is small at recording-master
bitrates.

**Why FLAC rather than AAC for recorded audio:** AAC encoders add 1024–2112
samples of priming delay (21–44 ms), and Matroska/most consumers do not
reliably compensate for it, which shifts audio against video. FLAC has no
delay, is lossless (the edit-then-export path stays at one lossy
generation), and costs about 0.4–0.8 Mbit/s for voice. Export converts to
AAC.

## 6. Audio path

```
RT callback ─copy─▶ SPSC float ring (2 s) + SPSC marker queue {hostNs, frames, flags}
                          │  (binary_semaphore release: lock-free wake)
                          ▼ pump thread (1 per source)
          level meter ─▶ timestamp smoother ─▶ boundary splitter (T0/pauses/S)
                          ─▶ drift controller ─▶ swresample (compensating) ─▶ delay line + fades
                          ─▶ FLAC encoder ─▶ [packet queue] ─▶ mux thread ─▶ file
```

**Real-time rules** for the RT callback: no locks, no allocation, no logging,
no syscalls except the semaphore wake. It writes into preallocated
lock-free rings and increments atomic counters.

### 6.1 Timestamp smoothing

Per-callback host timestamps jitter slightly, and the device sample clock
runs at its own rate (±100 ppm, i.e. up to ±0.36 s/hour against the host
clock). `AudioTimestampSmoother` fits `hostNs = a + b·sampleIndex` by least
squares over a sliding 20 s window (O(1) incremental sums, values centered for
numeric stability). It provides:

* a jitter-free host time for any input sample index (used for
  sample-accurate splits);
* the measured device rate (`driftPpm` in stats and manifest).

A residual larger than 20 ms marks a **discontinuity** (lost buffers, device
reset, sleep). The smoother re-anchors at the new point, and the drift
controller performs a hard correction.

### 6.2 Drift control

The audio file is sample-counted: output sample `n` is at session time
`n / 48000`. For each chunk, the writer computes:

```
target  = sessionTime(smoothedHost(firstSample)) × 48000         (fractional samples)
actual  = outputSamplesProduced + swr_get_delay(out)              (where the next sample will land)
error   = actual − target
```

* `|error| ≤ 20 ms`: **soft** correction with
  `swr_set_compensation(round(−error), 48000)`. This proportional controller
  over a one-second horizon changes the resampling ratio by at most 0.5 %
  (clamped). Steady-state error for a 300 ppm clock is about 0.3 ms.
* `|error| > 20 ms`: **hard** correction. Insert silence (we are behind:
  samples were lost) or drop input (we are ahead). The event is counted and
  written to the manifest.
* swresample runs with `SWR_FLAG_RESAMPLE`, so compensation also works when
  device and output rates are both 48 kHz.

This keeps every audio file aligned to host time, and therefore to video, for
arbitrarily long sessions, without the editor needing per-clip drift factors.

### 6.3 Levels

The pump thread computes per-channel peak and RMS (peak hold with 20 dB/s
decay). It publishes them through atomics, and the UI polls at display rate.
Meters work before recording starts because they belong to the live source,
not the writer.

## 7. Container and crash safety

**Decision: Matroska (`.mkv`) for every recorded track.**

| Option | Crash behavior | Start offsets | Codecs | Verdict |
|---|---|---|---|---|
| Plain MP4 | `moov` written at the end, so a crash leaves an **unplayable** file | edit lists | limited | ✗ |
| Fragmented MP4 | complete fragments survive | needs edit lists/`tfdt`; empty-`moov` complicates priming/offsets | AAC/FLAC ok, PCM awkward | viable |
| Hybrid MP4 (fMP4 → rewrite on stop) | good | good | — | extra full-file rewrite on stop |
| **Matroska** | complete clusters survive; FFmpeg demuxes truncated files | **absolute block timestamps** (first PTS = track offset, no edit lists) | anything (H.264/HEVC/FLAC/PCM) | **✓** |

Matroska's 1 ms timestamp precision is far below audible or visible A/V sync
thresholds. Exported deliverables are MP4.

Muxer configuration:

* `cluster_time_limit = 1000 ms`, `cluster_size_limit = 4 MB`: an unfinished
  cluster lives in muxer memory, so this bounds what a crash can lose.
* CRC-32 per cluster (default), which lets the salvage pass detect a torn
  tail.
* A custom `AVIOContext` (`FileSink`) owns the file descriptor. The mux
  thread calls `avio_flush` at least every 500 ms (data reaches the kernel and
  survives an **app** crash) and `fsync` every 10 s (survives most **OS**
  crashes or power loss). Finalize uses `F_FULLFSYNC` on macOS.
* File preallocation (`F_PREALLOCATE`/`fallocate`) is a planned option for
  spinning disks; it is off by default because APFS is copy-on-write.
* `ENOSPC`/`EIO` become `ErrorCode::OutOfSpace` / `IoError`. The session stops
  gracefully and finalizes every track it still can.

**Expected loss on crash:** app crash ≤ about 1.5 s per track; kernel panic or
power loss ≤ about 11 s.

## 8. Manifest, registry, and recovery

```
<project>/
  project.json                      (written when the session completes)
  media/screen/screen-<sid>.mkv
  media/camera/camera-<sid>.mkv
  media/audio/microphone-<sid>.mkv, system-audio-<sid>.mkv
  recordings/<sid>/session.json     (manifest, atomic rewrite)
  recordings/<sid>/session.lock     (exclusive lock held while recording)
<app data>/recordings/active/<sid>.json   (registry entry → session dir)
```

* `session.json` is rewritten atomically (write `.tmp`, `fsync`, `rename`,
  `fsync` the directory) on every state change and every 5 s as a
  checkpoint. It contains the host anchor `T0`, pause intervals, and per-track
  file, codec, offsets, stats and errors.
* While recording, the session holds an **exclusive OS lock** on
  `session.lock`. The OS releases it on process death, including `SIGKILL`
  or a crash, so "is the owner alive?" needs no PID bookkeeping and is immune
  to PID reuse.
* **On launch**, `RecoveryService` scans the registry. For each entry whose
  lock can be acquired and whose manifest is not `completed`:
  1. For each unfinished track, **salvage-remux** (stream copy) into a fresh
     Matroska file, stopping at the first corrupt or truncated packet. This
     rebuilds Cues (fast seeking) and the duration.
  2. Verify the salvaged file (open, probe, last timestamp), then replace the
     original atomically.
  3. Update the manifest to `recovered` with per-track recovered durations.
  4. `services::RecordingImporter` builds `project.json`, so the editor opens
     the recovered recording like a normal one, in sync.
* Recovery never runs on a session whose lock is held by another live
  instance.

## 9. Disk space

`DiskSpaceMonitor` samples free space on the output volume every 2 s and
estimates the write rate (EMA of the sum of all tracks' bytes):

| Level | Condition (defaults) | Action |
|---|---|---|
| OK | — | — |
| Warning | < 5 GB free **or** < 15 min at current rate | UI banner with remaining time |
| Critical | < 1 GB free **or** < 2 min at current rate | graceful auto-stop (`StopReason::DiskFull`), all files finalized |

## 10. Device loss

| Event | Behavior |
|---|---|
| Camera unplugged | Track stays open; the pacer stops duplicating after `maxHold` (the file gets a timestamp gap, counted as `gapSlots`); the UI shows "No signal". Automatic re-attach of a returning device and stall intervals in the manifest are planned. |
| Microphone lost | Pump sees no data; at resume or stop the drift controller fills silence, so the file stays continuous and in sync; status "Microphone disconnected". |
| ScreenCaptureKit stream stopped (display unplugged, window closed) | Screen track finalizes at the last frame; session continues; UI notified. |
| Encoder error | That track is finalized and marked failed; others continue. |

## 11. Statistics

`RecordingStats` (polled by the UI and CLI at 4–30 Hz):

* session: state, duration, pause count, disk level, free bytes, projected
  remaining time, total write rate;
* per video track: frames captured, encoded, duplicated, dropped
  (queue-full, source-reported, late), queue depth, encoder name, hardware
  flag, bytes written;
* per audio track: peak/RMS per channel, drift ppm, hard corrections, overflow
  samples, bytes written, active flag.

"Dropped frames" in the UI = `droppedQueueFull + droppedBySource`.
Duplicates caused by a static screen are expected and not reported as drops.

## 12. Platform backends

### macOS (implemented)
* **Screen:** ScreenCaptureKit. `SCShareableContent` enumerates displays,
  windows and applications. `SCContentFilter` modes: display (optionally
  excluding Lectern's own windows), single window
  (`initWithDesktopIndependentWindow`), application
  (`includingApplications`). NV12 (`420v`), BT.709 matrix, sRGB color space
  (WindowServer color-matches P3 panels), `queueDepth` 6, cursor optional,
  GPU scaling to the target size with preserved aspect ratio.
* **System audio:** ScreenCaptureKit audio (macOS 13+), 48 kHz stereo,
  excluding Lectern's own audio. A Core Audio process-tap backend (macOS
  14.2+, no screen permission needed) is planned.
* **Camera:** AVFoundation (`AVCaptureSession` + `AVCaptureVideoDataOutput`,
  NV12, `alwaysDiscardsLateVideoFrames`), format chosen as the best mode ≤
  target size that supports the target fps. Timestamps are converted from
  the session synchronization clock to host time.
* **Microphone:** Core Audio HAL IOProc on the chosen device (no
  AVCaptureSession), so the RT path and latency properties are under our
  control.
* **Permissions:** `CGPreflightScreenCaptureAccess` /
  `CGRequestScreenCaptureAccess`, and `AVCaptureDevice` authorization for
  camera and microphone. Without microphone permission Core Audio delivers
  **silence**, not an error, so permission is checked before starting.

### Windows (implemented; compile-checked, **not yet run**)
`src/platform/windows`, built for Windows 10 1903+ with MSVC or MinGW-w64.
Every file compiles against the Windows SDK headers (MinGW-w64 14), but none
has run on Windows yet, so treat this as unverified until the device tests
pass there.
* **Screen:** Windows.Graphics.Capture through the raw WinRT ABI (no
  C++/WinRT): `IGraphicsCaptureItemInterop` (monitor or window), a
  free-threaded `Direct3D11CaptureFramePool` (3 buffers) that signals an
  event. A thread at the MMCSS "Capture" priority drains the pool to the
  newest frame and throttles conversion to the recording rate. The throttle
  fires on the trailing edge, so the last change before the screen goes idle
  is never lost.
* **Screen conversion:** `ID3D11VideoProcessor` converts BGRA → NV12 (sRGB →
  BT.709 limited range, letterboxed into the output size), then one NV12
  readback into pooled buffers (`media::VideoFramePool`). Drivers without a
  video processor fall back to BGRA readback with CPU conversion.
* **Screen timestamps and options:** timestamps are `SystemRelativeTime`
  (QPC). Cursor capture follows the setting (Windows 10 2004+). The yellow
  border is switched off where allowed (Windows 11). Applications are
  captured through their largest window. Lectern's own windows are excluded
  with `WDA_EXCLUDEFROMCAPTURE`.
* **Camera:** Media Foundation source reader in asynchronous mode, so stop
  never waits on a stalled device. It picks the best native format: within
  bounds, reaching the frame rate, the closest rate, then the cheapest
  conversion. Advanced video processing converts to NV12 (with hardware MJPEG
  decoders where available).
* **Camera timestamps** use the first that is plausible:
  - the device's QPC reference (`MFSampleExtension_DeviceReferenceSystemTime`)
  - a QPC-based sample time
  - the sample time anchored to arrival (`capture::ArrivalAnchoredClock`: a
    minimum filter with a drift leak, unit-tested)
* **Microphone and system audio:** WASAPI shared mode, event-driven on a
  thread at the MMCSS "Pro Audio" priority. System audio is loopback of the
  default render device; it includes Lectern's own output (per-process
  exclusion via process loopback is planned). 16/24/32-bit PCM and float are
  converted to float. Timestamps come from `u64QPCPosition`. Device removal
  is reported as an interruption.
* **Permissions:** the CapabilityAccessManager consent store (device policy,
  user switch, desktop-app switch) for camera and microphone. "Request" opens
  the matching Settings page, because Windows has no runtime prompt for
  desktop apps.
* **Global hotkeys:** `RegisterHotKey` on a message-only window owned by the
  UI thread. Qt's event loop dispatches `WM_HOTKEY` to it.
* **Editor audio output:** WASAPI shared render with automatic format
  conversion (`AUTOCONVERTPCM`).
* **Encoders:** NVENC/QSV/AMF/Media Foundation, upload from NV12 (zero-copy
  D3D11 frames are planned).

### Linux (interface ready; backend pending)
xdg-desktop-portal ScreenCast → PipeWire node (DMA-BUF where possible),
PipeWire audio (source and sink monitor), V4L2 or PipeWire camera,
VA-API/NVENC encoders.

Until those backends land, the synthetic backend keeps the engine buildable
and testable on every OS.

## 13. Testing

* **Synthetic sources** (`capture/synthetic`) generate test patterns and tones
  with configurable frame rate, **clock drift (ppm)**, timestamp jitter,
  stalls and gaps. In manual mode, tests advance a `ManualClock`, so an hour
  of recording runs in seconds with deterministic results.
* **Sync test:** the synthetic video flashes white and the synthetic audio
  clicks at every whole host second, with the audio clock drifting by
  300 ppm. The test decodes both files and asserts every flash/click pair is
  within one frame / 5 ms.
* **Pause/resume test:** output durations equal recorded time, keyframe after
  each resume, no samples from paused intervals.
* **Crash test:** a child process records synthetic sources and is
  `SIGKILL`ed mid-session. Recovery must produce decodable files with the
  expected duration (± 1.5 s) and preserved offsets.
* **Real-device smoke tests** (`lectern-rec`) on each OS: list devices,
  record screen/camera/mic for a few seconds, probe the outputs.

## 14. Verification status

| Area | How it was verified |
|---|---|
| Sync, drift, pauses, gaps, late starts | Deterministic end-to-end tests with synthetic sources (`RecordingSessionTest`) |
| One-hour sessions | `LongRecordingTest` (label `long`): 1 h simulated, 250 ppm drift → worst A/V offset 0.27 ms at the end, exact sample/frame counts, flat memory |
| Crash recovery | `RecoveryTest`: real child process SIGKILLed mid-recording, salvaged, imported |
| Disk stalls / disk full | `MuxWorker.DiskStall…` (keyframe-safe dropping), `DiskFullStopsGracefully…` |
| Thread safety / memory safety | All engine suites pass under ThreadSanitizer and Address+UB sanitizers |
| macOS ScreenCaptureKit, AVFoundation camera, VideoToolbox zero-copy | Device smoke tests and `lectern-rec` on an M1 MacBook Air (macOS 26.4) |
| ScreenCaptureKit system audio | Recorded on the same machine (silence, as nothing was playing) |
| Core Audio microphone | Implemented; **not yet recorded on hardware** because microphone permission was not granted on the test machine (`LECTERN_TEST_MICROPHONE=1 ctest -L device` verifies it) |
| Global hotkeys (macOS, Carbon) | `DeviceSmoke.RegistersAndRemovesGlobalHotkeys`; the app logs registration of ⌘⇧R / ⌘⇧P |
| Editor audio output (Core Audio) | `DeviceSmoke.AudioOutputPullsAudioInRealTime` (plays digital silence; 47.9k frames/s pulled; Bluetooth device latency 161 ms reported and compensated) |
| Windows backends (WGC, D3D11 conversion, MF camera, WASAPI, hotkeys, audio output) | **Compile-checked only** (MinGW-w64 against the Windows SDK headers, with the project's warning flags); not built with MSVC, not run |
| Camera timestamp mapping for devices without a usable clock | `ArrivalAnchoredClock` unit tests: jitter removed, ±300 ppm drift followed within 0.5 ms over 5 min, re-anchoring on jumps |

## 15. Known limitations (current)

* The Windows backends have not been run on Windows yet (§12); Linux capture
  is not implemented (synthetic sources are used).
* Windows system audio (loopback) includes Lectern's own sounds.
* Global hotkeys are implemented on macOS (verified) and Windows (unverified);
  on Linux the in-app shortcuts and the floating HUD are available.
* Phone cameras arrive in Phase 1C and plug in as another `IVideoSource` /
  `IAudioSource` whose host timestamps come from the clock-sync model.
* A window's size change is scaled into the fixed output size chosen at start
  (aspect ratio preserved). There is no mid-recording resolution change.
* Microphones with more than 2 channels record the first two channels.
