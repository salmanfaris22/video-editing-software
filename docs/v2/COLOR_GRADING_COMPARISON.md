# Color grading — what Lectern is missing vs DaVinci Resolve, and every grading tool compared

Date: 2026-10-06. Lectern statuses were checked against the source (commit
`aacef0d` plus the looks / copy-grade / compare work in progress). Details of
other products come from general product knowledge (Resolve 19–21, Baselight 6,
Premiere Pro 25, Final Cut Pro 11, …) and are version-dependent: verify against
the vendor's manual before building an item, as with every v2 document.

Marks: ✅ Lectern has it · 🟡 partly · ⬜ missing.
In the comparison matrices: ● full · ◐ basic / partial / via add-on · – none.
**P** = priority (P0 first … P3 specialist). **Step** = the step in
[ROADMAP_PHASES.md](ROADMAP_PHASES.md) Phase 1 that delivers it.

Related: [DAVINCI_COLOR_PAGE.md](DAVINCI_COLOR_PAGE.md) (Resolve's Color page
control by control), [COLOR_EFFECTS_PARITY.md](COLOR_EFFECTS_PARITY.md)
(the whole color and effects checklist).

---

## 1. In one minute

**Lectern has today** (after the Color page rebuild, 2026-10-06):
- A Resolve-style Color page: Gallery / Looks / LUTs, the viewer with wipe,
  side by side, bypass (⇧D) and highlight (⇧H), the node graph, a Color
  Space Transform and effects panel, the clip strip, Primaries wheels,
  Curves over the histogram, Qualifier, Window, Magic Mask and Scopes (2-up).
- Primaries: Lift / Gamma / Gain / Offset wheels with master jog wheels, Temp,
  Tint, Contrast + Pivot, Exposure, Brightness, Shadows, Highlights, Color
  Boost, Saturation, Hue, one-click Auto Balance; custom curves Y, R, G, B.
- **Grade only part of the picture:** up to 8 serial nodes per clip, each
  with its own grade and a selection — circle / rectangle / gradient windows
  (handles on the viewer), an HSL color key picked on the viewer (e.g. only
  the pen), the person or the background (AI segmentation), or the outside
  of any of these.
- 16 tuned cinematic looks (Oppenheimer, Dark Green, Teal & Orange, Bleach
  Bypass …) with Amount, live thumbnails, hover preview, apply to all, and
  My Looks.
- Film effects: grain, halation, glow, film print emulation (3 stocks),
  vignette, lens blur, background blur.
- Repair: spatial noise reduction (separate Luma and Chroma thresholds,
  Radius) and Sharpen (Amount, Radius, Coring) — identical on CPU and GPU,
  real time at 1080p.
- Grade management: copy / paste (⌘C / ⌘V), grade of the previous clip (=),
  to next / all clips, gallery stills (grab, apply, wipe against).
- 3D LUTs (.cube with mix) plus 4 camera-log conversions; input color
  management incl. HLG/PQ HDR tone-mapped to SDR at 10 bits.
- GPU rendering (Metal) that matches the CPU reference for every feature.
- An AI assistant (MCP) that grades, adds nodes, applies looks, reads scopes
  and renders frames.

**The 12 biggest gaps vs Resolve now, in the order they matter:**

| # | Gap | P |
|---|---|---|
| 1 | Float, scene-linear, managed pipeline (ACES / RCM working space, HDR output) | P0 |
| 2 | Temporal noise reduction, and noise reduction in a chosen node (spatial NR with luma / chroma and sharpen with coring are done) | P1 |
| 3 | Tracking, so windows follow the subject | P1 |
| 4 | Grade keyframes (wheels, curves and nodes animated over time) | P1 |
| 5 | Soft clip, ganged curves, curve intensity | P2 |
| 6 | Polygon and Bézier windows; several windows per node | P1 |
| 7 | Shot match and chart match | P1 |
| 8 | Viewer zoom; scopes in % and nits; 4-up | P1 |
| 9 | Timeline grade, color groups, shared and parallel / layer-mixer nodes | P2 |
| 10 | Qualifier matte finesse (clean, blur, shrink / grow); RGB and 3D keys | P2 |
| 11 | HDR zone wheels, Log wheels, Primaries bars, Mid/Detail | P2 |
| 12 | Color Warper, Color Slice, RGB Mixer | P2 / P3 |

**Score vs Resolve's color features** (the 152 rows in §2): **54 ✅ · 19 🟡 · 79 ⬜** — about 36 % fully and 42 % counting partial rows as half (on the morning of 2026-10-06: 12 % / 18 %). For everyday grading Lectern now covers far more than the number suggests: the rows it has are the ones colorists use on every shot.

---

## 2. What Lectern is missing compared with DaVinci Resolve (full list)

### 2.1 Color pipeline and color management

| # | Resolve | Lectern now | P | Step |
|---|---|---|---|---|
| 2.1.1 | 32-bit float processing in every node | 🟡 GPU shader works in float, but curves and LUTs are sampled from 8-bit tables and the frame is 8-bit between passes | P0 | 1.2 |
| 2.1.2 | Scene-linear / wide-gamut working space (DaVinci Wide Gamut / Intermediate) | ⬜ display-referred Rec.709 | P1 | 1.2 |
| 2.1.3 | Resolve Color Management: input space per clip from metadata | ✅ from file tags, with a per-clip override | – | 1.2 |
| 2.1.4 | RCM timeline (working) space and output space choice | ⬜ always Rec.709 out | P0 | 1.2 |
| 2.1.5 | ACES (ACEScct / ACEScc, IDT / ODT) | ⬜ | P1 | 1.2 |
| 2.1.6 | Color Space Transform per node (gamut + tone mapping options) | 🟡 CST panel: input color space + camera log (4 logs); output fixed Rec.709 / Gamma 2.4 | P1 | 1.2 |
| 2.1.7 | Camera log formats (ARRI LogC3/4, RED Log3G10, Sony, Canon, Panasonic, Blackmagic, Fujifilm, Nikon, DJI, Apple) | 🟡 Apple Log, S-Log3, V-Log, LogC3 | P1 | 1.11 |
| 2.1.8 | HDR sources tone-mapped for SDR | ✅ HLG and PQ with highlight roll-off | – | 1.2 |
| 2.1.9 | HDR grading and HDR timelines (PQ / HLG, up to 10 000 nits) | ⬜ | P2 | 1.2 |
| 2.1.10 | Dolby Vision analysis and trims, HDR10+ metadata | ⬜ | P3 | – |
| 2.1.11 | Gamut mapping / gamut limiter (saturation knee) | ⬜ | P2 | 1.2 |
| 2.1.12 | 3D LUT per clip (input / output), per timeline, viewer-only LUT | 🟡 per-clip .cube with mix amount | P1 | 1.11 |
| 2.1.13 | Generate a LUT from a grade (17 / 33 / 65 point) | ⬜ | P2 | 1.11 |
| 2.1.14 | 10-bit and higher source decode | ✅ 10-bit when a clip is color-managed | – | 1.2 |
| 2.1.15 | Data levels per clip (video / full range) | ⬜ automatic only | P2 | 1.2 |
| 2.1.16 | Viewer uses the display's ICC profile (Mac color sync) | ⬜ | P2 | 1.2 |
| 2.1.17 | External monitoring (DeckLink / UltraStudio clean feed) | ⬜ | P3 | – |
| 2.1.18 | 10-bit / ProRes / DNxHR mastering export | ⬜ 8-bit H.264 / HEVC, correctly tagged Rec.709 | P2 | – |

### 2.2 Primaries

| # | Resolve | Lectern now | P | Step |
|---|---|---|---|---|
| 2.2.1 | Lift / Gamma / Gain / Offset wheels with master jog wheels | ✅ | – | 1.4 |
| 2.2.2 | Editable Y R G B number fields under each wheel | 🟡 shown as read-outs, not editable | P1 | 1.4 |
| 2.2.3 | Temp / Tint | ✅ own scale | – | 1.4 |
| 2.2.4 | Contrast + Pivot | ✅ | – | 1.4 |
| 2.2.5 | Mid/Detail (local contrast) | ⬜ | P1 | 1.4 |
| 2.2.6 | Color Boost | ✅ | – | 1.4 |
| 2.2.7 | Shadows / Highlights | ✅ | – | 1.4 |
| 2.2.8 | Saturation / Hue | ✅ | – | 1.4 |
| 2.2.9 | Lum Mix | ⬜ | P2 | 1.4 |
| 2.2.10 | Auto Balance | ✅ | – | 1.4 |
| 2.2.11 | White-balance picker (click something neutral) | ⬜ | P1 | 1.4 |
| 2.2.12 | Black-point / white-point pickers on Lift / Gain | ⬜ | P2 | 1.4 |
| 2.2.13 | Primaries Bars mode | ⬜ | P2 | 1.4 |
| 2.2.14 | Log Wheels (shadow / midtone / highlight with low / high range) | ⬜ | P2 | 1.4 |
| 2.2.15 | HDR Wheels: zones Black … Specular, exposure and color per zone, zone falloff | ⬜ | P2 | 1.4 |
| 2.2.16 | Per-wheel reset, reset all | ✅ | – | 1.4 |
| 2.2.17 | Fine adjust with a modifier key | ✅ ⇧ on wheels, jog wheels and fields | – | 1.4 |

### 2.3 Curves

| # | Resolve | Lectern now | P | Step |
|---|---|---|---|---|
| 2.3.1 | Custom curves Y / R / G / B | ✅ add, drag, remove points | – | 1.6 |
| 2.3.2 | Ganged / un-ganged channels, intensity per channel | ⬜ | P2 | 1.6 |
| 2.3.3 | Soft clip (low, high, softness) | ⬜ | P2 | 1.6 |
| 2.3.4 | Hue vs Hue | ✅ | – | 1.6 |
| 2.3.5 | Hue vs Sat | ✅ | – | 1.6 |
| 2.3.6 | Hue vs Lum | ✅ weighted by saturation | – | 1.6 |
| 2.3.7 | Lum vs Sat | ✅ | – | 1.6 |
| 2.3.8 | Sat vs Sat | ✅ | – | 1.6 |
| 2.3.9 | Sat vs Lum | ✅ | – | 1.6 |
| 2.3.10 | Click the viewer to add a curve point at that color | ✅ Pick adds the point at the clicked color | – | 1.6 |
| 2.3.11 | Six-vector preset points (R Y G C B M) | ✅ Six vectors button | – | 1.6 |

### 2.4 Hue, saturation and channel tools

| # | Resolve | Lectern now | P | Step |
|---|---|---|---|---|
| 2.4.1 | Color Warper — hue / saturation mesh | ⬜ | P3 | – |
| 2.4.2 | Color Warper — chroma / luma mesh | ⬜ | P3 | – |
| 2.4.3 | Color Slice (hue slices with density, subtractive saturation) | ⬜ | P2 | – |
| 2.4.4 | RGB Mixer (channel mixer, monochrome, preserve luminance, swap) | ⬜ | P2 | – |
| 2.4.5 | Color Compressor / color stabilizer (ResolveFX) | ⬜ | P3 | – |

### 2.5 Secondaries — the qualifier

| # | Resolve | Lectern now | P | Step |
|---|---|---|---|---|
| 2.5.1 | HSL qualifier (hue / sat / lum ranges with softness) | ✅ hue / sat / lum ranges with softness, picked on the viewer | – | 1.7 |
| 2.5.2 | Luma qualifier | ✅ all hues + a luminance range | – | 1.7 |
| 2.5.3 | RGB qualifier | ⬜ | P2 | 1.7 |
| 2.5.4 | 3D qualifier (stroke-pick in 3D color space) | ⬜ | P2 | 1.7 |
| 2.5.5 | Picker add / subtract / feather on the viewer | 🟡 pick (one click); no add / subtract strokes | P1 | 1.7 |
| 2.5.6 | Matte finesse: pre-filter, clean black / white, clip, blur, in/out ratio, shrink / grow, denoise | 🟡 softness only; no clean black/white, blur, shrink/grow | P1 | 1.7 |
| 2.5.7 | Highlight view (matte on gray / black / white, high contrast) | ✅ Highlight (⇧H): the selection in color over gray | – | 1.7 |
| 2.5.8 | Invert the key | ✅ | – | 1.7 |

### 2.6 Windows and masks

| # | Resolve | Lectern now | P | Step |
|---|---|---|---|---|
| 2.6.1 | Circle and linear (rectangle) windows | ✅ circle and rectangle | – | 1.8 |
| 2.6.2 | Polygon and curve (Bézier) windows | ⬜ | P1 | 1.8 |
| 2.6.3 | Gradient window | ✅ | – | 1.8 |
| 2.6.4 | Softness (inside / outside, per edge), opacity, invert | ✅ softness, invert (no opacity) | – | 1.8 |
| 2.6.5 | Combine windows (add / subtract / intersect) and with the qualifier | 🟡 one window per node, multiplied with the key and the person mask | P1 | 1.8 |
| 2.6.6 | On-viewer handles | ✅ move, resize, rotate on the viewer | – | 1.8 |
| 2.6.7 | Window presets | ⬜ | P3 | 1.8 |
| 2.6.8 | External matte / alpha as a key input | ⬜ | P2 | 1.13 |

### 2.7 Tracking

| # | Resolve | Lectern now | P | Step |
|---|---|---|---|---|
| 2.7.1 | Cloud tracker: pan, tilt, zoom, rotate, 3D (perspective) | ⬜ | P1 | 1.9 |
| 2.7.2 | Point tracker | ⬜ | P1 | 1.9 |
| 2.7.3 | AI tracker (IntelliTrack) | ⬜ | P2 | 1.9 |
| 2.7.4 | Track forward / back, by frame, interactive correction, frame vs clip mode | ⬜ | P1 | 1.9 |
| 2.7.5 | Stabilizer in the tracker palette | ⬜ | P2 | 1.9 |
| 2.7.6 | FX tracker (drives effect positions) | ⬜ | P2 | 1.9 |

### 2.8 AI color tools (Resolve "Neural Engine", mostly Studio)

| # | Resolve | Lectern now | P | Step |
|---|---|---|---|---|
| 2.8.1 | Magic Mask — person | ✅ person / background as a node selection, segmented every frame | – | 1.10 |
| 2.8.2 | Magic Mask — objects and features (face, hair, clothes) | ⬜ | P2 | 1.10 |
| 2.8.3 | Shot Match (match one clip to another) | ⬜ | P1 | 1.12 |
| 2.8.4 | Color Match to a chart (ColorChecker) | ⬜ | P2 | 1.12 |
| 2.8.5 | Auto color / auto balance | ✅ Auto Balance | – | 1.12 |
| 2.8.6 | Film Look Creator (one effect for a whole film look) | 🟡 looks + film print emulation + grain, halation, glow | P1 | 1.11 |
| 2.8.7 | Depth Map (depth-based fog and grades) | ⬜ | P2 | – |
| 2.8.8 | Face Refinement (skin smoothing, eyes, lips) | ⬜ | P2 | – |
| 2.8.9 | Relight (virtual lights from surface normals) | ⬜ | P3 | – |
| 2.8.10 | AI noise reduction (UltraNR) | ⬜ | P2 | – |
| 2.8.11 | Super Scale (AI upscaling) | ⬜ | P3 | – |
| 2.8.12 | Smart Reframe | ⬜ (layout presets only) | P2 | – |

### 2.9 Node graph and grade structure

| # | Resolve | Lectern now | P | Step |
|---|---|---|---|---|
| 2.9.1 | Several corrections per clip (serial nodes) | ✅ up to 8 serial nodes after node 01 | – | 1.3 |
| 2.9.2 | Timeline grade (one grade over the whole timeline) | ⬜ | P1 | 1.3 |
| 2.9.3 | Color groups (group pre-clip / post-clip grades) | 🟡 "apply to the whole recording" copies a grade to all screen or all camera clips (a copy, not a live link) | P1 | 1.13 |
| 2.9.4 | Enable / disable a node (⌘D), labels, reset | ✅ enable / disable, rename, reset, reorder | – | 1.3 |
| 2.9.5 | Bypass all grades (⇧D) | ✅ ⇧D | – | 1.14 |
| 2.9.6 | Parallel node, layer mixer (with blend modes) | ⬜ | P2 | 1.13 |
| 2.9.7 | Outside node | ✅ invert (grade outside the selection) | – | 1.13 |
| 2.9.8 | Shared nodes (one node used by many clips) | ⬜ | P2 | 1.13 |
| 2.9.9 | Splitter / combiner, key mixer | ⬜ | P3 | 1.13 |
| 2.9.10 | Compound nodes | ⬜ | P3 | 1.13 |
| 2.9.11 | Local / remote grade versions | ⬜ | P2 | 1.3 |
| 2.9.12 | Node cache / render cache | ⬜ | P2 | – |
| 2.9.13 | Node tree presets | ⬜ | P3 | 1.13 |

### 2.10 Repair, texture and sizing (Motion Effects, Blur, Key, Sizing palettes)

| # | Resolve | Lectern now | P | Step |
|---|---|---|---|---|
| 2.10.1 | Temporal noise reduction | ⬜ | P1 | – |
| 2.10.2 | Spatial noise reduction | 🟡 edge-preserving bilateral with Luma and Chroma thresholds and Radius, identical on CPU and GPU, real time at 1080p; no Faster / Better / Enhanced modes or Blend, and it runs after the grade instead of in a chosen node | P1 | – |
| 2.10.3 | Sharpen (radius, coring, level) | ✅ unsharp mask with Amount, Radius and Coring (noise is not sharpened), identical on CPU and GPU | – | – |
| 2.10.4 | Blur palette (per-channel radius, H/V ratio) | 🟡 Lens Blur (uniform) on the Color page | P2 | – |
| 2.10.5 | Mist / diffusion | ⬜ | P2 | – |
| 2.10.6 | Motion blur (synthetic) | ⬜ | P3 | – |
| 2.10.7 | Input sizing (zoom, pan, tilt, rotate, flip) | 🟡 clip transform on the Edit page | – | – |
| 2.10.8 | Output / node / reference sizing | ⬜ | P3 | – |
| 2.10.9 | Stabilization | ⬜ | P2 | 1.9 |
| 2.10.10 | Deflicker, dead pixel fixer, dust buster, patch replacer | ⬜ | P3 | – |
| 2.10.11 | Key palette (key input / output gain and offset) | ⬜ | P2 | 1.13 |
| 2.10.12 | Camera Raw palette (BRAW, R3D, ARRIRAW, ProRes RAW decode settings) | ⬜ | P3 | – |

### 2.11 Looks and film emulation (ResolveFX used on the Color page)

| # | Resolve | Lectern now | P | Step |
|---|---|---|---|---|
| 2.11.1 | Look presets with strength | ✅ 16 tuned looks, Amount, apply to all | – | 1.11 |
| 2.11.2 | Film grain | ✅ same noise on CPU and GPU, moves 24×/s | – | – |
| 2.11.3 | Halation | ✅ | – | – |
| 2.11.4 | Glow / bloom | ✅ | – | – |
| 2.11.5 | Print film emulation (e.g. Kodak 2383 LUTs) | ✅ Warm Print, Cool Print, Soft Negative | – | 1.11 |
| 2.11.6 | Vignette | ✅ | – | – |
| 2.11.7 | Lens blur, aperture diffraction, chromatic aberration | ⬜ | P3 | – |
| 2.11.8 | Film damage, gate weave | ⬜ | P3 | – |
| 2.11.9 | Dehaze, Contrast Pop, Beauty | ⬜ | P2 | – |

### 2.12 Grade management

| # | Resolve | Lectern now | P | Step |
|---|---|---|---|---|
| 2.12.1 | Copy / paste grade | ✅ ⌘C / ⌘V, right-click menu, Color menu | – | 1.3 |
| 2.12.2 | Apply the previous clip's grade (=) | ✅ = | – | 1.3 |
| 2.12.3 | Apply a grade to many clips at once | ✅ to next, to all, paste to selected clips | – | 1.3 |
| 2.12.4 | Append a node to selected clips | ⬜ | P2 | 1.13 |
| 2.12.5 | Gallery: grab still, label, compare | ✅ Grab Still ⌥⌘G | – | 1.11 |
| 2.12.6 | Apply a grade from a still | ✅ double-click a still | – | 1.11 |
| 2.12.7 | PowerGrades (shared across projects) | 🟡 My Looks (saved grades across projects; correction + look, not nodes) | P2 | 1.11 |
| 2.12.8 | Memories (⌥1 … 8 save, ⌃1 … 8 recall) | ⬜ | P2 | 1.11 |
| 2.12.9 | Import / export grades (.drx) and stills | ⬜ | P2 | 1.11 |
| 2.12.10 | Grade keyframes (dynamic / static, per node, per window) | 🟡 the data model can animate scalar color values; no UI; wheels and curves are not animatable | P1 | 1.14 |
| 2.12.11 | Clip strip flags, filters, color groups | ⬜ | P3 | 1.14 |
| 2.12.12 | Lightbox (grid of every clip) | ⬜ | P3 | 1.14 |

### 2.13 Viewer, compare and scopes

| # | Resolve | Lectern now | P | Step |
|---|---|---|---|---|
| 2.13.1 | Split-screen wipe (graded vs ungraded, still, previous clip, version) | ✅ against the ungraded picture or a still | – | 1.11 |
| 2.13.2 | Side-by-side compare | ✅ | – | 1.11 |
| 2.13.3 | Highlight (matte) view | ✅ | – | 1.7 |
| 2.13.4 | Viewer zoom and pan (fit, 100 %, 200 %) | ⬜ | P1 | 1.14 |
| 2.13.5 | Enhanced / cinema viewer | ⬜ | P2 | 1.14 |
| 2.13.6 | Loop playback, J K L on the Color page | 🟡 play / pause only | P1 | 1.14 |
| 2.13.7 | Waveform, RGB parade, vectorscope, histogram | ✅ | – | 1.5 |
| 2.13.8 | Vectorscope skin-tone line, 75 % / 100 % targets | ✅ | – | 1.5 |
| 2.13.9 | Scope scale in % and HDR nits | 🟡 10-bit (0–1023) only | P1 | 1.5 |
| 2.13.10 | Scopes computed on the GPU at full resolution in real time | 🟡 CPU on the preview frame | P1 | 1.5 |
| 2.13.11 | 1-up / 2-up / 4-up scopes | 🟡 1-up and 2-up | P2 | 1.5 |
| 2.13.12 | Scope settings (low-pass, brightness, extents, graticule) | ⬜ | P2 | 1.5 |
| 2.13.13 | CIE chromaticity scope | ⬜ | P3 | 1.5 |
| 2.13.14 | Safe areas and guides | ⬜ | P3 | 1.14 |

### 2.14 Hardware, collaboration, workflow

| # | Resolve | Lectern now | P | Step |
|---|---|---|---|---|
| 2.14.1 | Grading panels (Micro / Mini / Advanced, Tangent, Loupedeck) | ⬜ | P3 | – |
| 2.14.2 | Multi-user collaboration and Blackmagic Cloud | ⬜ | P3 | – |
| 2.14.3 | Remote grading | ⬜ | P3 | – |
| 2.14.4 | Proxy / optimized media for grading | ⬜ | P2 | – |
| 2.14.5 | Render cache (smart / user) | ⬜ | P2 | – |
| 2.14.6 | Undo per grade, full history | ✅ one undo step per edit | – | – |
| 2.14.7 | Scripting / automation API | ✅ MCP tools (set_color, set_wheel, set_curve, get_scopes, render_frame, …) | – | – |

### 2.15 Count

| Section | Rows | ✅ | 🟡 | ⬜ |
|---|---|---|---|---|
| 2.1 Pipeline and management | 18 | 3 | 4 | 11 |
| 2.2 Primaries | 17 | 9 | 1 | 7 |
| 2.3 Curves | 11 | 9 | 0 | 2 |
| 2.4 Hue / channel tools | 5 | 0 | 0 | 5 |
| 2.5 Qualifier | 8 | 4 | 2 | 2 |
| 2.6 Windows | 8 | 4 | 1 | 3 |
| 2.7 Tracking | 6 | 0 | 0 | 6 |
| 2.8 AI tools | 12 | 2 | 1 | 9 |
| 2.9 Nodes and structure | 13 | 4 | 1 | 8 |
| 2.10 Repair, texture, sizing | 12 | 1 | 3 | 8 |
| 2.11 Looks and film | 9 | 6 | 0 | 3 |
| 2.12 Grade management | 12 | 5 | 2 | 5 |
| 2.13 Viewer and scopes | 14 | 5 | 4 | 5 |
| 2.14 Hardware and workflow | 7 | 2 | 0 | 5 |
| **Total** | **152** | **54** | **19** | **79** |


---

## 3. Every color grading tool compared

### 3.1 The tools

| Tool | Maker | Kind | How grades are built | Price model | Known for |
|---|---|---|---|---|---|
| **DaVinci Resolve Studio** | Blackmagic Design | Full grading suite + NLE | Nodes | Free version; Studio one-time license | The industry default; AI tools (Magic Mask, Relight, Depth Map); free tier |
| **Baselight** | FilmLight | High-end grading system | Layer stack | Turnkey systems / enterprise licenses | Feature films and premium TV; Base Grade, X Grade, Chromogen, Truelight color science |
| **Flame** | Autodesk | Finishing + compositing | Node "Batch" + timeline | Subscription | Commercial finishing; Color Corrector / Color Warper inside a compositor; ML keys |
| **SCRATCH** | Assimilate | Dailies, grading, finishing | Layer stack | Subscription | Real-time dailies, VR / 360, on-set Live FX |
| **Mistika Boutique / Ultima** | SGO | Grading + finishing | Layers / effects stack | Subscription (Boutique) / systems (Ultima) | Stereo 3D, VR, HDR finishing |
| **Media Composer (Symphony features)** | Avid | NLE with grading option | Per-clip correction + relational (source / program) grades | Subscription | Broadcast and long-form; relational grading |
| **Lumetri Color** | Adobe (Premiere Pro, After Effects) | NLE built-in | Effect stack (several Lumetri instances, adjustment layers) | Creative Cloud subscription | Easiest pro grading for editors: Basic, Creative looks, HSL Secondary |
| **Final Cut Pro** | Apple | NLE built-in | Effect stack of corrections | One-time purchase | Color Board / Wheels / Curves, Balance and Match Color, Magnetic Mask |
| **Magic Bullet Colorista + Looks** | Maxon (Red Giant) | Plug-ins for Premiere / AE / FCP | Plug-in inside the host | Subscription (Maxon One / Red Giant) | Guided correction, skin overlay, look presets with "tools" (lens, film, diffusion) |
| **Dehancer Pro** | Dehancer | Film emulation plug-in | Plug-in inside the host | One-time / subscription | Real negative + print film profiles, halation, bloom, grain, gate weave |
| **FilmConvert Nitrate** | FilmConvert | Film emulation plug-in | Plug-in inside the host | One-time | Camera-specific profiles into film stocks, scanned grain |
| **Colourlab Ai** | Colourlab | AI grading assistant | Stand-alone + host plug-ins | Subscription | AI shot matching and look design across a whole timeline |
| **CapCut / KineMaster** | ByteDance / KineMaster | Consumer editors | Filter + adjust sliders per clip | Free + subscription | Filters, quick adjust, HSL (CapCut) |
| **Lectern** | – | Recorder + editor | One grade per clip (layers planned) | – | Recording-to-edit in one app; AI assistant grades over MCP |

### 3.2 Matrix — grading suites

| Capability | Resolve Studio | Baselight | Flame | SCRATCH | Mistika | Avid Symphony | **Lectern** |
|---|---|---|---|---|---|---|---|
| Primary wheels (lift / gamma / gain / offset) | ● | ● | ● | ● | ● | ● | ● |
| Log / zone wheels (HDR palette) | ● | ● (Base Grade) | ◐ | ◐ | ◐ | – | – |
| Custom RGB curves | ● | ● | ● | ● | ● | ● | ● |
| Hue / sat curves | ● | ● | ● | ● | ● | ◐ | – |
| Mesh warper (hue-sat / chroma-luma) | ● | ● (X Grade) | ● (Color Warper) | – | – | – | – |
| Qualifier / keyer | ● | ● | ● | ● | ● | ◐ (vectors) | ● (HSL) |
| Power windows / shapes | ● | ● | ● | ● | ● | – | ◐ (circle, rectangle, gradient) |
| Tracking | ● | ● | ● | ● | ● | – | – |
| AI masks (person / object) | ● | ◐ (Face Track) | ● (ML keys) | – | – | – | ◐ (person / background) |
| Several corrections per clip | ● nodes | ● layers | ● nodes | ● layers | ● | ◐ | ● (serial nodes) |
| Group / timeline grades | ● | ● | ● | ● | ● | ● (relational) | ◐ |
| Shot match / chart match | ● | ◐ | – | ◐ | ◐ | ◐ | – |
| Gallery / stills / grade library | ● | ● | ◐ | ● | ● | ◐ | ● |
| Split-screen compare | ● | ● | ● | ● | ● | ● | ● |
| Grade keyframes | ● | ● | ● | ● | ● | ● | – |
| Noise reduction | ● | ● | ● | ◐ | ● | – | ◐ (spatial, luma / chroma) |
| Film grain / halation / glow | ● | ● | ◐ | ◐ | ◐ | – | ● |
| ACES / scene-linear pipeline | ● | ● (Truelight) | ● (OCIO) | ● | ● | ● (ACES) | – |
| Input color management from metadata | ● | ● | ● | ● | ● | ● | ● |
| HDR grading + Dolby Vision | ● | ● | ● | ● | ● | ◐ | – |
| Scopes | ● | ● | ● | ● | ● | ● | ● |
| Control panels | ● | ● | ● | ● | ● | ◐ | – |
| GPU real-time | ● | ● | ● | ● | ● | ◐ | ● |
| AI assistant that edits and grades | – | – | – | – | – | – | ● (MCP) |

### 3.3 Matrix — editors' built-in color, plug-ins and consumer apps

| Capability | Lumetri (PR / AE) | Final Cut Pro | Colorista + Looks | Dehancer | FilmConvert | Colourlab Ai | CapCut | **Lectern** |
|---|---|---|---|---|---|---|---|---|
| Basic sliders (exposure, contrast, highlights, shadows, temp, tint, saturation) | ● | ● | ● | ◐ | ● | ● | ● | ● |
| 3-way wheels | ● | ● | ● | – | – | ◐ | – | ● (4 wheels + jog) |
| Custom RGB curves | ● | ● | ● | – | ◐ | ◐ | ● | ● |
| Hue / sat curves | ● | ● | ● | – | – | – | ◐ (HSL) | – |
| HSL secondary / color key | ● | ● | ● | – | – | – | – | ● |
| Shape masks with tracking | ● | ◐ | ◐ | – | – | – | ◐ | ◐ (no tracking) |
| AI subject mask | ◐ | ● (Magnetic Mask) | – | – | – | – | ● | ● (person / background) |
| Look presets with intensity | ● | ◐ (LUTs, presets) | ● | ● | ● | ● | ● (filters) | ● |
| Real film-stock emulation (negative + print) | – | – | ◐ | ● | ● | ● | – | ◐ (print stage) |
| Grain / halation / bloom | ◐ (faded film, no halation) | – | ● (grain, diffusion) | ● | ● | ◐ | ◐ | ● |
| Auto color / white balance | ● | ● | ● (guided) | – | – | ● | ● | ● |
| Shot / color match | ● | ● | – | – | ◐ (CineMatch) | ● (AI) | – | – |
| Compare view (before / after) | ● | ● | ◐ | ● | ◐ | ● | ◐ | ● |
| Copy / paste grade | ● | ● | ● | ● | ● | ● | ● | ● |
| Scopes | ● | ● | ◐ | – | – | ◐ | – | ● |
| Log camera input | ● | ● | ● | ● | ● (camera profiles) | ● | ◐ | ◐ (4 logs) |
| HDR | ● | ● | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ (input only) |
| AI assistant grades for you | – | – | – | – | – | ◐ (AI matching) | – | ● (MCP) |

### 3.4 The best idea in each tool, and what Lectern takes from it

| Tool | Standout idea | Lectern item |
|---|---|---|
| DaVinci Resolve | Nodes; Magic Mask; HDR zone wheels; Color Warper; Film Look Creator | Nodes ✅, Magic Mask ✅ (person / background), Looks + film tools ✅; 2.2.15, 2.4.1 next |
| Baselight | **Base Grade**: exposure-based balancing that keeps colors natural when pushed hard; **X Grade**: change a color by pinning points on the picture; **Chromogen**: a look-development tool; **Texture Equalizer**: contrast by detail frequency | Make the primaries photometric (stops) in the float pipeline; viewer pins for HSL curves (2.3.10); look builder; Mid/Detail (2.2.5) |
| Flame | Grading inside a compositor: the matte from any node feeds the grade | Key input for correctors (2.6.8) |
| SCRATCH | Live grading on set and real-time dailies | Live grade in the recorder preview (later) |
| Mistika | Stereo and 360 finishing | Not planned (specialist) |
| Avid Symphony | **Relational grading**: one correction per source file reused wherever that source appears | Grade per media source as well as per clip (color groups, 2.9.3) |
| Lumetri | **Basic + Creative** split: correct first, then a look with an Intensity slider; Faded Film; Shadow / Highlight tint | Looks with Amount ✅; add a Fade and split-tone control |
| Final Cut Pro | **Balance Color** and **Match Color** as one-click actions; Magnetic Mask | Auto Balance ✅; shot match (2.8.3); magic mask (2.8.1) |
| Colorista | **Guided correction** and a skin overlay on the scopes | Guided first-time grade in the Color page; skin-tone line ✅ |
| Magic Bullet Looks | Looks built from real-world "tools": lens, filter, film, diffusion | Looks palette groups by stage (lens, film, print) |
| Dehancer | **Film chain**: negative → print, halation, bloom, grain, film breath, gate weave | Film look tools (2.11.2–2.11.8) as one "Film" palette |
| FilmConvert | Profiles per camera model so the same stock looks the same on every camera | Camera profiles in input color management |
| Colourlab Ai | **AI match a whole timeline** to a reference | `match_shot` MCP tool + Shot Match (2.8.3) |
| CapCut | One-tap filters with an amount slider, HSL for beginners | Looks thumbnails with Amount ✅; simple HSL tab |

---

## 4. Where Lectern can win

1. **The assistant grades for you.** No grading tool lets Claude, ChatGPT or
   Codex read the scopes, try a grade, render a frame, look at it and fix it
   (MCP tools `get_scopes`, `render_frame`, `set_wheel`, `set_curve`,
   `apply_look`). Resolve, Baselight and Lumetri have nothing like this.
2. **Recording-aware color.** Lectern knows which clip is the screen and
   which is the camera, so a grade can be applied to "all camera clips"
   with one click (already there), and the screen can stay untouched.
3. **Looks that are tuned, not just LUTs.** Each Lectern look is built from
   the same primaries and curves the user can open and adjust, so a look
   is a starting point, not a black box.
4. **One app, simple to Pro.** Simple users get looks and one-click
   balance; colorists get the Color page with wheels, curves and scopes.

---

## 5. Order of work for color (what to build next)

Done on 2026-10-07: all six HSL curves with picker and six vectors; spatial
noise reduction (luma / chroma) and sharpen with coring.
Done on 2026-10-06: cinematic looks with Amount and My Looks; copy / paste
and spread grades; wipe, side by side, bypass; nodes with windows, a color
key and person / background; gallery stills; grain, halation, glow and film
print emulation; the Resolve-style Color page.

| Order | What | Rows | Step |
|---|---|---|---|
| 1 | Tracking for windows (point / cloud), magic mask follows already | 2.7 | 1.9 |
| 2 | Grade keyframes (correction, nodes, windows) | 2.12.10 | 1.14 |
| 3 | Float linear pipeline, working / output space, ACES | 2.1.1–2.1.5 | 1.2 |
| 4 | Polygon / Bézier windows, several windows per node | 2.6.2, 2.6.5 | 1.8 |
| 5 | Shot match, chart match | 2.8.3, 2.8.4 | 1.12 |
| 6 | Timeline grade, color groups, parallel / layer mixer / shared nodes | 2.9.2–2.9.8 | 1.13 |
| 7 | Viewer zoom, scopes % / nits / 4-up | 2.13.4, 2.13.9, 2.13.11 | 1.5 / 1.14 |
| 8 | HDR zone wheels, Log wheels, Primaries bars, Mid/Detail | 2.2.5, 2.2.13–2.2.15 | 1.4 |
| 9 | Temporal noise reduction; noise reduction in a chosen node (before the grade) | 2.10.1, 2.10.2 | – |
