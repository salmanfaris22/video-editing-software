# Performance

Performance is requirement #1. This document defines budgets, the rules that
keep hot paths fast, how the engine is instrumented, and how to profile on
each OS. Numbers marked *measured* come from this repository's tools on real
hardware; everything else is a target.

---

## 1. Targets

| Area | Target |
|---|---|
| Cold start to interactive window | < 1.0 s (no media work at startup; QML precompiled with `qmlcachegen`) |
| Record button → recording | < 50 ms (sources are pre-started; only `T0` is set) |
| Stop → editor usable | < 1 s for a 1 h recording (no transcoding; probe + project write only) |
| Recording overhead, 1080p60 screen + 720p30 camera + 2 audio, Apple Silicon | < 10 % of one performance core on the CPU side; hardware encoders do the rest |
| A/V sync error over a 3 h recording | < 5 ms (drift-controlled) |
| Timeline interactions (drag, trim, zoom) | < 8 ms UI-thread work per frame; **no media decoding during drag/trim** |
| Playback | 1080p60 multi-layer smooth on recent hardware; 4K via hardware decode or proxies |
| Memory | bounded by cache budgets; independent of recording length |

## 2. Rules for hot paths

1. **No per-frame allocation** in capture, encode, render or audio paths.
   Frames come from pools: `CVPixelBufferPool`, `AVBufferPool`, our
   preallocated rings.
2. **No full-frame CPU copies** unless the target is a software codec. Count
   the copies per path (see §3) and treat any increase as a regression.
3. **The audio RT callback** does `memcpy` into a lock-free ring, one atomic
   counter update, and one semaphore release. Nothing else.
4. **Capture callbacks never block.** They `try_push` into bounded queues;
   overflow drops and counts.
5. **The UI thread never waits on the engine.** It reads snapshots (atomics
   or a short mutex around a small struct) at display rate.
6. **QML never rebuilds large object trees on playhead changes.** The
   timeline is a scene-graph item; the playhead is its own node.
7. **Background work is bounded and cancellable** (thumbnails, waveforms,
   proxies, probing).
8. Logging on hot paths is rate-limited; RT threads never log.

## 3. Copy budget per path

| Path | macOS (current) | Windows (planned) | Linux (planned) |
|---|---|---|---|
| Screen → hardware encoder | **0 CPU copies** (IOSurface → VideoToolbox) | 0 (WGC texture → D3D11 video processor → NVENC/QSV/AMF) | 0–1 (DMA-BUF → VA-API) |
| Screen → software encoder | 1 (`av_hwframe_transfer_data`) | 1 (staging readback) | 1 |
| Camera → hardware encoder | 0 | 0–1 | 1 (V4L2 mmap → VA-API upload) |
| Microphone → encoder | 1 (RT → ring) + resample | same | same |
| Decode → preview (planned) | 0 (CVMetalTextureCache) | 0 (shared D3D11 device) | 0–1 |

## 4. Threads and queues (recording)

| Queue | Capacity | Overflow policy | Why this size |
|---|---|---|---|
| video capture → encode | 6 frames | drop newest, count | ≤ ScreenCaptureKit `queueDepth` (6), so surfaces are never starved |
| encode → mux | 64 MB | block encoder up to 2 s; then drop until the next keyframe | absorbs disk stalls of several seconds at 4K bitrates; never writes undecodable data |
| audio RT ring | 2 s | drop and count; drift controller re-aligns | covers scheduler hiccups and pump-thread delays |
| audio marker queue | 1,024 chunks | drop and mark discontinuity | ≥ 2 s of 2 ms callbacks |
| log queue | 8,192 messages | drop and count | logging must never block |

Thread naming (`lectern.<component>.<id>`) is applied everywhere, so
profilers show meaningful names.

## 5. Memory budgets

* Recording: O(1) in recording length — queues above plus encoder internals.
  Expected steady state below 150 MB for 4K60 + camera.
* Editor caches (planned, scaled to system RAM): decoded frames 512 MB
  (GPU), thumbnails 128 MB, composited frames 32 frames. LRU with pinning of
  the active playback window.
* Memory-pressure handling: macOS `DISPATCH_SOURCE_TYPE_MEMORYPRESSURE`,
  Windows `CreateMemoryResourceNotification`, Linux PSI
  (`/proc/pressure/memory`). On warning, shrink caches 50 %; on critical,
  drop all non-pinned entries and lower preview quality.

## 6. Instrumentation built into the engine

* `RecordingStats` (per-track counters, queue depths, encode latency, write
  rate, drift) are polled by the UI and printed by `lectern-rec --stats`.
* *(Planned, Phase 1H)* `ScopedTimer` / trace markers that compile to
  `os_signpost` (macOS, visible in Instruments), ETW TraceLogging (Windows,
  visible in WPA/PIX), and Perfetto/`perf` markers (Linux), compiled out in
  release builds unless `LECTERN_TRACE=ON`. Until then, every engine thread is
  named (`lectern.venc.screen`, `lectern.mux.camera`, …), so stock profilers
  already attribute work correctly.
* The log records encoder selection, negotiated formats, permissions, drift
  estimates, and every dropped-frame burst (rate-limited).

## 7. Profiling playbook

| Question | macOS | Windows | Linux |
|---|---|---|---|
| CPU hot spots | Instruments → Time Profiler | WPA (CPU sampling), VTune | `perf record -g`, Hotspot |
| GPU work | Instruments → Metal System Trace; Xcode GPU capture | PIX, GPUView, Nsight | RenderDoc, `intel_gpu_top`, `nvtop` |
| Encoder/decoder engine load | Activity Monitor → GPU History; `powermetrics` | Task Manager → Video Encode/Decode | `intel_gpu_top`, `nvidia-smi dmon` |
| Disk I/O | Instruments → File Activity; `fs_usage` | WPA (Disk I/O) | `iotop`, `biosnoop` |
| Allocation churn | Instruments → Allocations | WPA heap, VS diagnostics | `heaptrack` |
| Locks / waits | Instruments → System Trace | WPA (CPU precise) | `perf sched`, `offcputime` |

Procedure: reproduce with `lectern-rec` (headless, deterministic flags) or a
saved project, record a trace, identify the top cost, fix it, and add a
benchmark or test that guards the fix.

## 8. Benchmarks

* `lectern-rec --synthetic --duration N --stats` measures the pipeline
  without devices (encode throughput, queue behavior, write rate).
* `lectern_capture_tests` contains long-run tests (labelled `long`) for 1 h
  synthetic recordings in fast-forward mode (memory must stay flat).
* Real-device runs: `lectern-rec --display main --camera default --mic
  default --duration 600 --stats` on each reference machine; results are
  recorded below.

## 9. Measured results

Machine: MacBook Air M1 (8 GB), macOS 26.4.1, Debug build unless noted.

| Measurement | Result | How |
|---|---|---|
| Real recording, 1080p60 screen (1728×1080) + FaceTime camera, 10 s | 0 dropped frames on both tracks; **1.84 s CPU over 13.5 s** wall time (~14 % of one core, including start-up, camera warm-up and finalization); peak memory 95 MB; both tracks `h264_videotoolbox` zero-copy | `/usr/bin/time -l lectern-rec --display main --camera default --fps 60 --duration 10` |
| A/V sync, 17 s with 500 ppm audio drift, jitter, pause | worst offset **2.1 ms** | `RecordingSession.EndToEndSyncWithDriftJitterAndPause` |
| A/V sync after one hour with 250 ppm drift | worst offset **0.27 ms** at 59:40–60:00; measured drift 251.2 ppm; 0 hard corrections | `LongRecording.OneHourStaysInSyncWithFlatMemory` |
| Memory vs. recording length | RSS 25.8 MB at 10 min → 17.1 MB at 60 min (flat) | same test |
| Crash loss window | ≤ ~1.5 s per track (SIGKILL test recovers all but the last cluster) | `Recovery.SalvagesRecordingAfterSigkill` |
| Full unit + integration suite | 155 tests in ~30 s (incl. editor, playback and UI-controller tests) | `ctest --preset dev` |
| Hardware encoders verified | `h264_videotoolbox`, `hevc_videotoolbox` (plus `libx264`, `libx265` fallbacks) | `lectern-probe --encoders` |

## 10. Editor measurements (2026-10-05, re-verified 2026-10-06)

**2026-10-06 re-run:** `scripts/export-benchmark.sh` (or the commands below) on
`build/dev`; full `/usr/bin/time -l` output is appended to
`docs/benchmarks/export-YYYYMMDD.log`. PSNR vs the pre-optimization renderer
stays **46.6 dB** when export pixels match (`Export.*`, `QaRegression.*`).

Machine as in §9; **Release** build. Test project: 20 s recorded with
`lectern-rec --synthetic` (1080p screen, 720p camera, microphone, system
audio). Styled with a gradient background, 5 % screen padding, rounded
corners, a shadow and a circular bordered camera.

| Measurement | Result | How |
|---|---|---|
| Export 1080p30 H.264 + AAC (hardware encoder) | 20 s exported in **10.3 s (1.95× real time)**; 7.8 s CPU; peak memory 205 MB | `/usr/bin/time -l lectern-export … --resolution 1080p --fps 30` |
| Compositor optimization (same export) | 11.8 s → 10.3 s wall, **12.0 s → 7.8 s CPU (−35 %)**. Output identical to the eye: 46.6 dB PSNR against the previous renderer | `sample` profiles before/after; PSNR of a 1080p frame |
| Where export time goes now (busy samples) | swscale color conversion ~55 % (YUV→RGB per layer, RGB→NV12 for the encoder), memcpy 11 % (hardware-frame downloads), blending 7 % | `sample <pid> 5` |
| Preview playback | Real time with sound at preview size; the playhead tracks the device's played samples within one device period (tests assert ≤ 60 ms including interpolation, also under parallel load) | `Playback.*` tests |
| Audio output latency (Bluetooth headphones) | 161 ms reported by Core Audio and compensated in the clock | `DeviceSmoke.AudioOutputPullsAudioInRealTime` |

**Fixes made from profiling** (no visual change):
* Cache the background instead of painting a gradient every frame.
* Skip near-no-op rescales of whole layers (the source was decoded to the
  shown size, and rounding differed by 1 px).
* Blit the interior of rounded layers, so only the four corner squares go
  through the antialiased texture-brush path.
* Pre-render shadows at full size, and blend only the rim the layer doesn't
  cover.

**Next steps.**
* The remaining cost is the CPU round trip YUV → RGB → composite → YUV.
* The GPU compositor (QRhi, RENDERING_PIPELINE.md §2) keeps decoded surfaces
  on the GPU and composites there; expected 5–10× faster at 1080p.
* Until then, pipelining export (render frame N+1 while N encodes) would hide
  the roughly 2 s per 20 s of waiting on the hardware encoder and decoder.
