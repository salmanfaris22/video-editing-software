# Rendering Pipeline (Phases 1D–1G design)

The same GPU compositor renders the interactive preview and the final export.
Preview trades resolution for smoothness when needed; export always renders at
full configured quality from original media.

Status (2026-10-05): **preview and export render on the GPU** (Qt RHI:
Metal; Direct3D 11 compiled for Windows but not run yet) through
`src/render/GpuRenderer`, which draws the same `RenderPlan` as the CPU
compositor (QPainter). The CPU compositor stays as the reference for tests
and the fallback (`LECTERN_RENDERER=cpu`, or automatically when no GPU
backend starts). §0 describes what is built; §0.1 the GPU renderer; §1–§12
are the original design (zero-copy decode and float color are still to come,
see docs/v2/ROADMAP_PHASES.md).

---

## 0. As built

```
 Project snapshot ─▶ buildRenderPlan(t) ─▶ Compositor (QPainter, CPU) ─┬─▶ preview QImage ─▶ EditorPreviewItem (texture)
                    (pure data, tested)    ▲                           └─▶ export: BT.709 NV12 ─▶ VideoEncoder (VideoToolbox/…) ─▶ MP4
                                           │ FrameProvider: VideoReader (HW decode) ─▶ swscale (source matrix + range) ─▶ RGB32
 Audio: AudioReader (48 kHz stereo float, sample-exact) ─▶ AudioMixer ─┬─▶ SPSC ring ─▶ audio device (master clock)
                                                                       └─▶ AAC encoder (export)
```

### 0.1 GPU renderer (`src/render`, Phase 1.1)

* **Interface:** `editor::FrameRenderer` (`render` to RGB32, optional
  `renderNv12` for export). `makeRenderer()` returns the registered GPU
  renderer or the CPU compositor; the app and `lectern-export` call
  `render::installGpuRenderer()` at startup.
* **One frame:** pre-passes for layers that need them (grade into a
  layer-sized texture, separable Gaussian blur, person-mask mix for
  background blur), then one canvas pass: background (clear or gradient),
  per media layer an analytic rounded-box shadow and the layer quad
  (crop/zoom/fit/fill mapping identical to the compositor, mirror, rotation,
  SDF rounded/circle shape, vignette, border, opacity), and text/subtitle
  runs as overlays rasterized by `Compositor::drawText/drawSubtitle` and
  cached while unchanged. Premultiplied alpha, BGRA8 textures (RGB32's byte
  order: uploads and readback without conversion).
* **Color:** the same 8-bit curve tables as the CPU (`colorCurves`) as a
  256×1 texture, saturation, and the 3D LUT as an RGBA16F 3D texture.
* **Export:** the canvas is converted to BT.709 limited NV12 on the GPU
  (luma pass + 2×2-averaged chroma pass) and only the planes are read back.
  The exporter decodes the next frame on a worker thread meanwhile.
* **Tests:** `tests/render/GpuRendererTest.cpp` — every scene against the CPU
  compositor, a GPU vs CPU export, and 1080p frame-time benchmarks.
  `LECTERN_RENDER_PROFILE=1` prints per-frame timings.

**Render plan** (`editor/RenderPlan.h`, pure data). For time t it resolves:
* **Media layers.** Screen and camera clips go into their layout slots
  (`LayoutPresets`, aspect-aware: side-by-side stacks vertically on portrait
  canvases), adjusted by the clip transform. If the layout needs a source
  that isn't recorded, it falls back to the one that is. Each layer carries
  the style (screen padding, radius, shadow; camera shape, border, mirror),
  opacity, color grading and effects: zoom (source crop), blur and vignette.
* **Overlays** (images and videos), placed freely.
* **Text** with presets: title, lower third, caption, callout, with 150 ms
  edge fades.
* **One subtitle** on top.

**Compositor** (`editor/Compositor.h`):
* **Background:** cached once, blitted per frame.
* **Layers:** decoded to roughly the shown size, drawn 1:1 when within 2 px,
  placed on whole pixels.
* **Shapes:** rounded rectangles blit their interior and use an antialiased
  texture-brush path only in the corner squares; circles use the brush path.
* **Shadows:** a quarter-resolution blur scaled once and cached per shape.
  Only the rim around an opaque layer is blended.
* **Color grading:** per-channel lookup tables plus saturation.
* **Blur:** three box passes, at reduced resolution for large radii.
* **Text:** device-metric fonts with hinting off, so wrapping is the same at
  every output size. Subtitles are laid out with one `QTextLayout` that sizes
  the box and draws the lines. A subtitle steps above a title drawn in its
  place, like broadcast captions.
* **Missing media** draws a placeholder.

**Playback** (`editor/PlaybackEngine.h`):
* **Audio** is mixed about 150 ms ahead into a lock-free ring that the device
  drains: Core Audio on macOS, WASAPI on Windows, a silent real-time clock
  otherwise. The device starts after 60 ms of prefill.
* **The clock** is frames played by the device minus its reported latency.
  If the mixer falls behind, the clock pauses, so video waits rather than
  drifting.
* **Video** is rendered by a second thread at exact frame times of the
  project rate, at the preview's pixel size.
* **Seek, pause and edits** restart the pipeline. A generation counter drops
  stale mixes, and edits reach both threads through immutable snapshots.

**Export** (`editor/Exporter.h`):
* Runs the same plan, compositor and mixer offline, at exact frame times.
* Video: BT.709 limited-range NV12 into the preferred encoder (hardware
  first).
* Audio: AAC 192 kb/s, accounted sample-exactly per frame.
* MP4/MOV with the index at the front (`+faststart`).
* Writes a `.partial` file that is renamed on success; cancel or failure
  leaves nothing behind. A canvas of another aspect is letterboxed.
* Also available as `lectern-export`.

**Correctness checks** (`tests/editor`, `tests/unit/media`):
* **Colors:** screen and camera colors match BT.709 decoding at their layout
  positions.
* **Shapes and effects:** rounded corners, circles, padding, gradients, zoom
  regions, text and subtitles.
* **Subtitles** lose no words at any canvas width from 600 to 900 px. This is
  a regression test for a box-versus-wrap mismatch.
* **Mixer:** sample-exact against a lossless ramp, through ripple cuts, gain,
  mute, solo, fades and volume automation.
* **Playback:** the playhead equals what the device has played, also under
  parallel test load. Seeks play the new position's samples and never stale
  ones.
* **Export:** picture and sound across a cut, faststart, hardware encoder,
  cancel.
* **Encoder color:** BGRA input is encoded with the encoder's matrix
  (regression test: it was BT.601).

**Not built yet:**
* the GPU compositor (§2)
* proxies and caches beyond the shown-size decode (§8–9)
* frame blending for retimed clips
* transitions
* ducking
* zero-copy D3D11 / VideoToolbox frames into the compositor

---

## 1. Overview

```
             ┌──────────── per active clip ────────────┐
 Media file ─▶ Demuxer ─▶ Decoder (HW → GPU surface)  ─┐
                                    │                  │  FrameCache (GPU, LRU, budgeted)
                                    └── SW fallback ───┘
                                                       ▼
 Timeline snapshot ─▶ evaluate(t) ─▶ Compositor (QRhi) ─┬─▶ Preview: QQuickRhiItem texture (no copy)
                                                        └─▶ Export: offscreen target ─▶ encoder (HW, zero-copy where possible)
 Audio: decode ─▶ resample 48 kHz float ─▶ mixer (gain envelopes, ducking) ─▶ device / export encoder
```

## 2. GPU API choice: Qt RHI

* Qt Quick already renders through **QRhi** (Metal on macOS, D3D11/D3D12 on
  Windows, Vulkan/OpenGL on Linux). Rendering the compositor with the
  **same `QRhi` instance** (via `QQuickRhiItem`, Qt ≥ 6.7) lets the preview
  texture appear in the scene graph with **zero copies and no cross-API
  synchronization**.
* Shaders are written once in Vulkan-style GLSL and compiled at build time
  to SPIR-V/MSL/HLSL with `qt_add_shaders` (qtshadertools).
* Export uses the same compositor on an offscreen `QRhi`, so preview and
  export are pixel-consistent by construction.
* Rejected: a bespoke Metal/D3D/Vulkan engine (three implementations plus
  texture sharing with Qt); OpenGL only (deprecated on macOS, weak on
  Windows).

## 3. Per-clip pipeline

```
decoded frame (YUV planes, native texture)
  → YUV→RGB (per-source matrix BT.601/709/2020 + range, from metadata)
  → crop (UV rect)                       ┐
  → transform (affine: position/scale/rotation/flip, from Animated<T>)   ├─ fused into one draw where possible
  → shape mask (SDF rounded-rect/circle) + border                         ┘
  → color adjustments  (single "uber" pass: exposure, brightness, contrast,
                        highlights, shadows, saturation, temperature, tint, gamma)
  → effects stack      (ordered; multi-pass where required)
  → drop shadow        (blurred mask, offset, opacity)
  → composite onto canvas (premultiplied alpha, track order)
```

* **Fusion:** pure per-pixel operations (YUV conversion, crop, color
  adjustments, opacity, shape mask, border) run in **one fragment shader**
  per layer. Only spatial effects (blur, sharpen, shadow) need extra passes.
* **Blur** is a separable Gaussian with progressive downsampling for large
  radii (cost roughly independent of radius). **Sharpen** is an unsharp mask
  reusing the blur. **Vignette** and **grayscale** are single-pass.
* **LUTs** (later) are a 3D texture lookup inside the color pass.

## 4. Color management

* Phase 1 working space: Rec.709 primaries, display-referred gamma-encoded
  RGBA8/RGBA16F. Compositing happens in gamma space, like most consumer
  editors. Blurs and shadows use a 16-bit float intermediate to avoid
  banding.
* Correct per-source decoding: matrix and range come from stream metadata
  (screen recordings are tagged BT.709 limited). Untagged SD content
  defaults to BT.601 and HD to BT.709.
* Export converts RGB → YUV (BT.709 limited) in the final shader pass and
  tags the stream accordingly.
* Later: a scene-linear working space (RGBA16F) and HDR (PQ/HLG) — the
  shader stages are already isolated for this.

## 5. Zero-copy interop

| Platform | Decode → compositor | Compositor → encoder (export) |
|---|---|---|
| macOS | VideoToolbox `CVPixelBuffer` (IOSurface) → `CVMetalTextureCache` → `MTLTexture` per plane → `QRhiTexture::createFrom` | render into IOSurface-backed `CVPixelBuffer` from a pool → `h264_videotoolbox` (`AV_PIX_FMT_VIDEOTOOLBOX`) |
| Windows | D3D11VA NV12 texture array → per-plane SRVs; Qt Quick uses **our** `ID3D11Device` (`QQuickGraphicsDevice::fromDeviceAndContext`) | render target → `AV_PIX_FMT_D3D11` frames → NVENC/QSV/AMF |
| Linux | VA-API surface → DRM PRIME (DMA-BUF) → `VkImage` (`VK_EXT_external_memory_dma_buf`) or `EGLImage` | Vulkan/VA-API interop where available; otherwise readback |
| Fallback | software decode → persistent staging buffer upload (1 copy) | GPU readback → software encoder (1 copy) |

## 6. Playback coordination

* **Master clock:** the audio output device clock during playback (video
  follows audio, avoiding audible glitches). When there is no audio, a host
  clock is used.
* Each vsync, the coordinator computes timeline time, evaluates the timeline
  snapshot, and requests frames `sourceTime(t)` from per-clip decoders that
  prefetch a bounded number of frames (default 6) ahead.
* If a frame is not ready by its deadline, the previous frame is held. The
  overrun counts toward the adaptive quality controller.
* **Scrubbing:** show the nearest cached frame or thumbnail immediately, then
  issue a keyframe seek plus decode-forward for the exact frame. In-flight
  stale requests are cancelled.

## 7. Preview quality

Modes: **Full**, **½**, **¼**, **Proxy**, **Auto**.

* The compositor renders the canvas at the chosen fraction of project
  resolution. Qt Quick scales it to the view.
* **Auto:** an EWMA of GPU frame time (QRhi timestamps) plus decode lateness.
  If it is over 85 % of the frame budget for 10 consecutive frames, step down
  one level. If it is under 50 % for 120 frames, step up. Steps are shown as
  "Low Resolution Preview".
* Proxy mode decodes proxy files instead of originals (see §8).
* **Export never uses proxies or reduced resolution.**

## 8. Proxies

* Generated in the background, cancellable, with progress, for media that
  exceeds decode budgets: 4K or more, HEVC 10-bit, very high bitrate, long-GOP
  phone footage, or a probe-measured decode speed below 1.5× real time.
* Format: H.264 (or ProRes Proxy on macOS when available), ½ or ¼
  resolution, all-intra or short GOP (good scrubbing), same timestamps as
  the original, audio copied.
* Stored in `proxies/<media fingerprint>-<variant>.mkv`. They are disposable
  and rebuilt when missing.
* Phone preview recordings (Phase 1C) become the proxy of the HQ file for
  free.

## 9. Caches

| Cache | Key | Budget (default) | Policy |
|---|---|---|---|
| Decoded frames (GPU) | media, stream, pts, scale | 512 MB VRAM (scaled to device) | LRU, current playback window pinned |
| Effect intermediates | size, format | pool | reuse; release on idle |
| Composited frames (paused/scrub) | timeline revision, t, quality | 32 frames | LRU |
| Thumbnails | media fingerprint, t, height | 128 MB RAM + disk | LRU |
| Waveform peaks | media fingerprint | mmap'd pyramids | on disk |

On memory pressure (OS notifications or our own accounting), caches shrink
in reverse order of rebuild cost.

## 10. Export

* Deterministic frame loop: `t = n × frameDuration(exportFps)`. Exact-frame
  decode from originals; compositor at export resolution; RGB→YUV in shader;
  hardware encoder when available, software fallback.
* Audio: offline mix at 48 kHz float with sample-exact gain envelopes, then
  AAC.
* MP4 with `+faststart`. Progress (frames done, elapsed, ETA from a moving
  average) and cancellation at frame granularity. The partial file is
  deleted on cancel.
* Runs in a worker **process** (`lectern-export`) speaking a JSON-lines
  progress protocol over stdio, which isolates codec crashes from the
  editing session. A worker thread is acceptable for the first
  implementation behind the same interface.

## 11. Threads

| Thread | Work |
|---|---|
| Qt Quick render thread | compositor draw calls for preview (`QQuickRhiItem::render`) |
| Decode workers (pool, 1 per active clip, bounded) | demux, decode, upload/interop |
| Audio thread | mixing into the device callback from a lock-free ring |
| Playback coordinator | clock, frame selection, prefetch requests |
| Background pool | thumbnails, waveforms, proxies, probing |

## 12. Effect plug-in model

```cpp
struct EffectDescriptor {
    std::string id;                   // "lectern.blur.gaussian"
    std::string name;
    std::vector<ParamDescriptor> params; // type, range, default, keyframeable
    EffectStage stage;                // ColorFusable | Spatial | Composite
};
class IEffectRenderer {               // created per QRhi
    virtual void prepare(QRhi&, const EffectParams&, QSize) = 0;
    virtual void record(QRhiCommandBuffer&, QRhiTexture* in, QRhiRenderTarget* out) = 0;
};
```

`ColorFusable` effects contribute a GLSL snippet and uniforms to the fused
color pass. `Spatial` effects get their own passes. The project stores
effects as `{id, version, params}`, which keeps them independent of the
implementation.
