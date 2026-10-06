# Color & Effects Parity — After Effects, Premiere Pro, DaVinci Resolve

Goal: bring Lectern's color and effects to the level of **Adobe After Effects**,
**Adobe Premiere Pro** (Lumetri) and **DaVinci Resolve** (Color page, ResolveFX,
Fusion), end to end. This document is the master list. Every feature the three
apps offer for color, effects, compositing and motion is listed once. Each
entry shows which of the apps has it and where Lectern stands today.

Scope: picture only. Audio effects (Fairlight, Essential Sound) belong in a
separate audio document. Editing operations are covered in ../TIMELINE_ENGINE.md.

Status (2026-10-05): **inventory and roadmap only, nothing in this document is
implemented yet** beyond the rows marked ✅. The ✅ rows were checked against
the source (`src/editor/ColorGrading.*`, `src/editor/RenderPlan.*`,
`src/timeline/Timeline.h`).

---

## 0. How to read the tables

| Mark | Meaning |
|---|---|
| ✅ | Lectern has it, tested |
| 🟡 | Lectern has part of it (see the note) |
| ⬜ | Missing |
| **AE / PR / DR** | ● the app has it natively · ◐ partly / via a different tool · – not in that app |
| **P0–P3** | Priority for Lectern: P0 = foundation, needed before anything else; P1 = expected by every user of a pro editor; P2 = pro / power users; P3 = specialist, later or never |

Lectern's audience is screen recordings, talking-head and tutorial videos, so
priorities favour what those videos need: clean skin, punchy screen footage,
LUTs, looks, blur/redaction, zooms, motion graphics and text. Resolve's
film-finishing features (HDR mastering, Dolby Vision, stereo 3D) rank low.

### 0.1 What Lectern has today

| Area | Lectern now |
|---|---|
| Basic color | exposure, brightness, contrast, saturation, temperature, tint (per clip, keyframable values in the model) |
| Color wheels | lift / gamma / gain wheels with master (3-way primaries) |
| LUTs | .cube import (copied into the project), mix amount; built-in Apple Log, Sony S-Log3, Panasonic V-Log, ARRI LogC3 → Rec.709 |
| Grade reuse | color presets; "apply to every screen/camera clip" |
| Effects | Gaussian-style blur, vignette, zoom (punch-in with center), background blur (person segmentation) |
| Transform | position, scale, opacity, with keyframes |
| Text | 4 presets and 11 in/out animations (fade, slide ×4, pop, zoom, typewriter, wipe, blur) |
| Pipeline | GPU renderer on Qt RHI (Metal; Direct3D 11 on Windows, untested) for preview and export, 8-bit RGB; the CPU QPainter compositor is the reference and fallback (2026-10-05) |

---

## 1. Foundation (must exist before "pro level" is possible)

None of the color or effect work below can match the reference apps without
these. They are the P0 work.

| # | Feature | AE | PR | DR | Lectern | P | Notes |
|---|---|---|---|---|---|---|---|
| 1.1 | GPU render pipeline (preview + export) | ● | ● (Mercury) | ● | ✅ | P0 | `src/render/GpuRenderer` on Qt RHI (Metal; D3D11 compiled for Windows, not run); matches the CPU compositor within 0.3/255 mean (tests/render) |
| 1.2 | 16-bit and 32-bit float processing | ● (8/16/32 bpc project) | ● (max bit depth) | ● (32-bit float always) | 🟡 | P0 | HDR/wide-gamut sources decode at 10 bits and convert in float (2026-10-06); the canvas is still 8-bit — RGBA16F canvas next |
| 1.3 | Linear-light compositing | ● (linearize working space) | ◐ | ● | ⬜ | P0 | Blur, blends and opacity must happen in linear light |
| 1.4 | Color-managed pipeline (input → working → output transforms) | ● (OCIO / ACES in AE 2024+) | ● (color management, 2025) | ● (RCM, ACES, OCIO-like) | ⬜ | P0 | Input color space per clip, working space, display and output transforms |
| 1.5 | Per-clip input color space / gamma tagging | ● (Interpret Footage) | ● (Override Media Color Space) | ● | ✅ | P0 | Auto from file tags or override: Rec.709, sRGB, Display P3, Rec.2020, HLG, PQ (Adjust panel, MCP `set_clip_color_space`) |
| 1.6 | Working spaces: Rec.709, sRGB, Rec.2020, DaVinci Wide Gamut / Intermediate, ACEScct, ACEScg | ● | ◐ | ● | ⬜ | P1 | |
| 1.7 | Display / viewer transform (what the screen shows) | ● | ● | ● | ⬜ | P1 | macOS EDR / Windows HDR display output |
| 1.8 | HDR timelines (PQ, HLG) and HDR export | ● | ● | ● | 🟡 | P2 | HLG/PQ sources are tone-mapped to SDR (BT.2408 reference white, highlight roll-off); HDR output not yet |
| 1.9 | Camera log/RAW decode: Apple Log, S-Log3, V-Log, LogC3/4, C-Log, N-Log, F-Log, BRAW, ProRes RAW, R3D | ◐ | ● | ● | 🟡 | P1 | 4 log curves today; RAW decode is P3 |
| 1.10 | Effect plug-in architecture (parameters, keyframes, GPU shader per effect) | ● | ● | ● | 🟡 | P0 | `effects[]` with type + params exists; needs a registry and a shader per effect (../RENDERING_PIPELINE.md §12) |
| 1.11 | Effect stack per clip (order, enable/disable, duplicate, copy/paste attributes) | ● | ● | ● | 🟡 | P1 | Fixed set today; no reordering or copy/paste |
| 1.12 | Adjustment layers (effects apply to everything below) | ● | ● | ● (adjustment clips) | ⬜ | P1 | |
| 1.13 | Render caching (smart cache, pre-render, cache to disk) | ● | ● | ● | ⬜ | P1 | |
| 1.14 | Proxy / optimized media | ● | ● | ● | ⬜ | P1 | Planned in ../RENDERING_PIPELINE.md §8 |
| 1.15 | Real-time playback at full rate with grades and effects | ◐ | ● | ● | ✅ | P0 | Styled, graded 1080p frame: 5.7 ms GPU vs 17 ms CPU on M1; export decodes ahead (decode-bound, ≈ 2.6–3.8× real time) |
| 1.16 | OpenFX plug-in host | – | – | ● | ⬜ | P3 | Third-party effects (Boris FX, Red Giant, etc.) |
| 1.17 | Adobe AE/PR plug-in host | ● | ● | – | ⬜ | P3 | Not realistic; listed for completeness |

---

## 2. Color — primaries and basic correction

| # | Feature | AE | PR (Lumetri) | DR | Lectern | P | Notes |
|---|---|---|---|---|---|---|---|
| 2.1 | Exposure | ● | ● | ● (Offset/Exposure) | ✅ | – | |
| 2.2 | Contrast with pivot | ● | ● (no pivot) | ● (pivot) | ✅ (contrast with pivot) | P1 | No pivot |
| 2.3 | Highlights / Shadows | ● (Shadow/Highlight) | ● | ● (HDR wheels, Highlights/Shadows sliders) | ✅ | P1 | Key for screen and webcam footage |
| 2.4 | Whites / Blacks | ● (Levels) | ● | ● | ⬜ | P1 | |
| 2.5 | Temperature / Tint | ● | ● | ● | ✅ | – | |
| 2.6 | Saturation | ● | ● | ● | ✅ | – | |
| 2.7 | Vibrance | ● | ● | ◐ (Color Boost) | ✅ (Color Boost) | P1 | Protects skin tones |
| 2.8 | Brightness | ● | ◐ | ◐ | ✅ | – | |
| 2.9 | Auto white balance / auto tone | ● (Auto Color/Levels/Contrast) | ● (Auto) | ● (Auto Balance, AI) | 🟡 (Auto Balance from the picture) | P1 | One-click fix for webcams |
| 2.10 | White-balance picker (eyedropper) | ● | ● | ● | ⬜ | P1 | |
| 2.11 | Lift / Gamma / Gain wheels | ● (Lumetri, Color Balance) | ● | ● | ✅ | – | |
| 2.12 | Offset wheel | – | ◐ | ● | ✅ (Offset wheel) | P1 | |
| 2.13 | Shadows / Midtones / Highlights wheels (Lumetri style) | ● | ● | ◐ | ✅ (Color page wheels) | P1 | Same math as 2.11; UI preset |
| 2.14 | Log wheels (Shadow/Midtone/Highlight with range control) | – | – | ● | ⬜ | P2 | |
| 2.15 | HDR zone wheels (Black, Dark, Shadow, Light, Highlight, Specular, Global) | – | – | ● | ⬜ | P2 | |
| 2.16 | Primaries bars (per-channel lift/gamma/gain) | – | – | ● | ⬜ | P2 | |
| 2.17 | Color boost, Midtone detail, Hue rotate, Luma mix | – | ◐ | ● | ⬜ | P2 | |
| 2.18 | Channel mixer (RGB matrix) | ● | – | ● (RGB Mixer) | ⬜ | P2 | Also B&W conversion |
| 2.19 | Levels (input/output black/white, gamma, per channel) | ● | ◐ | ◐ | ⬜ | P1 | |
| 2.20 | Invert / Negative | ● | ● | ● | ⬜ | P3 | |
| 2.21 | Black & White / Tint / Tritone | ● | ◐ | ● | ⬜ | P1 | Common look |
| 2.22 | Photo Filter (warming/cooling) | ● | – | ◐ | ⬜ | P2 | |
| 2.23 | Grade presets / looks gallery with thumbnails | ● (Lumetri Looks) | ● (Creative Looks) | ● (Gallery stills) | 🟡 | P1 | Presets exist; no thumbnails gallery |
| 2.24 | Match color between clips (shot match) | ● (Color Match) | ● (Color Match, AI) | ● (Shot Match) | ⬜ | P1 | Match the camera to the screen recording look |
| 2.25 | Copy / paste grade, apply to all of a source | ● | ● | ● | ✅ | – | `applyToRole` |
| 2.26 | Reset per section / per control | ● | ● | ● | 🟡 | P1 | Full reset only |
| 2.27 | Bypass / compare (before-after, split screen, wipe) | ◐ | ● | ● | ⬜ | P1 | |

## 3. Color — curves

| # | Feature | AE | PR | DR | Lectern | P | Notes |
|---|---|---|---|---|---|---|---|
| 3.1 | RGB / Luma custom curves (spline points) | ● (Curves) | ● (RGB Curves) | ● (Custom) | ✅ (Curves – Custom) | P1 | |
| 3.2 | Per-channel R, G, B curves | ● | ● | ● | ✅ | P1 | |
| 3.3 | Luma vs. Saturation | – | ● | ● | ⬜ | P2 | |
| 3.4 | Hue vs. Hue | – | ● | ● | ⬜ | P1 | |
| 3.5 | Hue vs. Saturation | – | ● | ● | ⬜ | P1 | |
| 3.6 | Hue vs. Luma | – | ● | ● | ⬜ | P1 | |
| 3.7 | Saturation vs. Saturation | – | ● | ● | ⬜ | P2 | |
| 3.8 | Saturation vs. Luma | – | – | ● | ⬜ | P2 | |
| 3.9 | Soft clip (highs/lows) | – | – | ● | ⬜ | P2 | |
| 3.10 | Curve eyedropper (pick a color in the viewer to add points) | – | ● | ● | ⬜ | P2 | |

## 4. Color — secondaries (qualifiers, windows, tracking)

| # | Feature | AE | PR | DR | Lectern | P | Notes |
|---|---|---|---|---|---|---|---|
| 4.1 | HSL qualifier (key by hue/sat/luma, soften, denoise) | ◐ (Change to Color / keys) | ● (HSL Secondary) | ● | ⬜ | P1 | Skin, sky, brand color |
| 4.2 | RGB qualifier | – | – | ● | ⬜ | P3 | |
| 4.3 | Luma qualifier | – | ◐ | ● | ⬜ | P2 | |
| 4.4 | 3D qualifier | – | – | ● | ⬜ | P3 | |
| 4.5 | Highlight / show matte view | – | ● | ● | ⬜ | P1 | |
| 4.6 | Power windows / masks: circle, linear, polygon, curve (bezier), gradient | ● (masks) | ● (masks on effects) | ● | ⬜ | P1 | |
| 4.7 | Mask feather, expansion, invert, opacity | ● | ● | ● | ⬜ | P1 | |
| 4.8 | Mask tracking (planar / point) | ● (Mask tracking, Mocha) | ● | ● | ⬜ | P1 | Track a face or a window for blur/grade |
| 4.9 | Object / person mask (AI) | ● (Roto Brush) | ◐ | ● (Magic Mask) | 🟡 | P1 | Person segmentation exists (background blur only) |
| 4.10 | Depth map (AI) | – | – | ● | ⬜ | P3 | |
| 4.11 | Combine qualifier + window (intersect, subtract) | ◐ | ◐ | ● | ⬜ | P2 | |
| 4.12 | Skin tone protection / face refinement | – | ◐ | ● (Face Refinement) | ⬜ | P1 | Talking-head videos |
| 4.13 | Vector / hue-range secondary (Hue/Saturation per range) | ● (Hue/Saturation) | ◐ | ● (Color Warper) | ⬜ | P2 | |
| 4.14 | Color Warper (hue-saturation and chroma-luma mesh) | – | – | ● | ⬜ | P3 | |
| 4.15 | Selective color / change color / change to color | ● | – | ◐ | ⬜ | P2 | |
| 4.16 | Leave Color (everything gray but one color) | ● | ◐ | ◐ | ⬜ | P2 | |

## 5. Color — node and layer structure

| # | Feature | AE | PR | DR | Lectern | P | Notes |
|---|---|---|---|---|---|---|---|
| 5.1 | Several correction layers per clip (stacked corrections) | ● (effect stack) | ● (multiple Lumetri) | ● (serial nodes) | ⬜ | P1 | Start with an ordered list of correctors |
| 5.2 | Node graph: serial, parallel, layer mixer nodes | – | – | ● | ⬜ | P2 | |
| 5.3 | Splitter / combiner (per-channel) nodes | – | – | ● | ⬜ | P3 | |
| 5.4 | Key mixer, external matte | – | – | ● | ⬜ | P3 | |
| 5.5 | Shared nodes / group grades (pre-clip, post-clip, timeline) | ◐ | ◐ | ● | ⬜ | P2 | Lectern equivalent: grade per source + timeline grade |
| 5.6 | Timeline-level grade | ◐ (adjustment layer) | ◐ | ● | ⬜ | P1 | |
| 5.7 | Versions of a grade per clip (local / remote) | – | – | ● | ⬜ | P3 | |
| 5.8 | Stills gallery, PowerGrades, grab still, wipe against still | – | – | ● | ⬜ | P2 | |
| 5.9 | Keyframing grades (dynamic, static, dissolve) | ● | ● | ● | 🟡 | P1 | Model supports animated values; UI does not |
| 5.10 | Grade copy between projects / export as LUT | ◐ | ● (export .cube) | ● (Generate LUT) | ⬜ | P2 | |
| 5.11 | Color Space Transform node / effect | ● | ◐ | ● | ⬜ | P1 | Needs §1 |
| 5.12 | Tone mapping and gamut mapping | ● | ● | ● | ⬜ | P1 | |

## 6. Color — scopes and monitoring

| # | Feature | AE | PR | DR | Lectern | P | Notes |
|---|---|---|---|---|---|---|---|
| 6.1 | Waveform (luma, RGB overlay) | ● | ● | ● | ✅ (Color page) | P1 | GPU compute on preview frame |
| 6.2 | RGB Parade | ● | ● | ● | ✅ | P1 | |
| 6.3 | Vectorscope (with skin-tone line, 75%/100% targets) | ● | ● | ● | ✅ (with skin-tone line and 75% targets) | P1 | |
| 6.4 | Histogram | ● | ● | ● | ✅ | P1 | |
| 6.5 | CIE chromaticity | – | – | ● | ⬜ | P3 | |
| 6.6 | False color / exposure warnings / clip indicators | ◐ | ◐ | ● | ⬜ | P2 | |
| 6.7 | Highlight out-of-gamut / broadcast safe | ● | ● | ● | ⬜ | P2 | |
| 6.8 | Reference wipe / split-screen compare | ◐ | ● | ● | ⬜ | P1 | Same as 2.27 |
| 6.9 | Color picker readout (RGB/HSL values under cursor) | ● (Info panel) | – | ● | ⬜ | P2 | |
| 6.10 | External reference monitor output | ● | ● | ● | ⬜ | P3 | |

## 7. Color — LUTs and looks

| # | Feature | AE | PR | DR | Lectern | P | Notes |
|---|---|---|---|---|---|---|---|
| 7.1 | 3D LUT import (.cube) | ● | ● | ● | ✅ | – | |
| 7.2 | LUT mix / intensity | ◐ | ● | ● (key output gain) | ✅ | – | |
| 7.3 | Other LUT formats (.3dl, .csp, .look) | ● | ◐ | ● | ⬜ | P3 | |
| 7.4 | 1D LUTs / shaper LUTs | ● | – | ● | ⬜ | P3 | |
| 7.5 | Camera manufacturer conversions | ◐ | ● | ● | 🟡 | P1 | 4 built in; add Canon C-Log2/3, Nikon N-Log, Fujifilm F-Log/F-Log2, DJI D-Log/D-Log M, LogC4, Blackmagic Film Gen5 |
| 7.6 | Creative looks library (film emulations, teal & orange, etc.) | ● | ● | ● (Film Look Creator) | 🟡 | P1 | Presets exist; need a curated library |
| 7.7 | Film Look Creator (halation, bloom, grain, gate weave, print density) | – | – | ● | ⬜ | P2 | |
| 7.8 | LUT placed before or after the grade | ● | ● (Input LUT / Creative) | ● | 🟡 | P1 | Fixed position today |
| 7.9 | Export grade as .cube | – | ● | ● | ⬜ | P2 | |

## 8. Color — image repair (ResolveFX Revival, Lumetri, AE)

| # | Feature | AE | PR | DR | Lectern | P | Notes |
|---|---|---|---|---|---|---|---|
| 8.1 | Temporal noise reduction | ● (Remove Grain) | ◐ | ● | ⬜ | P1 | Webcam noise in low light |
| 8.2 | Spatial noise reduction | ● | ● (Median, Reduce Noise) | ● | ⬜ | P1 | |
| 8.3 | Sharpen / unsharp mask | ● | ● | ● | ⬜ | P1 | |
| 8.4 | Soften & sharpen, midtone detail / clarity | – | ● (Sharpen in Lumetri) | ● | ⬜ | P1 | |
| 8.5 | Deflicker | – | ◐ | ● | ⬜ | P2 | LED / screen flicker in recordings |
| 8.6 | Dead pixel fixer | – | – | ● | ⬜ | P3 | |
| 8.7 | Dust buster / patch replacer / object removal | ● (Content-Aware Fill) | ● (Generative Extend; object removal 2025) | ● (Object Removal, Patch Replacer) | ⬜ | P2 | |
| 8.8 | Chromatic aberration removal | – | – | ● | ⬜ | P3 | |
| 8.9 | Lens distortion correction | ● (Optics Compensation) | ● (Lens Distortion) | ● | ⬜ | P2 | Wide-angle webcams |
| 8.10 | Dehaze | – | ● (Lumetri) | ● | ⬜ | P2 | |
| 8.11 | Super Scale / AI upscale | – | – | ● | ⬜ | P2 | |
| 8.12 | Beauty / face refinement (skin smoothing, eye/lip enhance) | ◐ | ◐ | ● | ⬜ | P1 | Talking heads |
| 8.13 | Relight (AI) | – | – | ● | ⬜ | P3 | |
| 8.14 | Stabilizer (warp stabilizer) | ● | ● | ● | ⬜ | P1 | Phone camera |
| 8.15 | Rolling shutter repair | ● | ● | ● | ⬜ | P3 | |

---

## 9. Effects — blur and sharpen

| # | Effect | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 9.1 | Gaussian Blur | ● | ● | ● | ✅ (blur) | – |
| 9.2 | Fast Box Blur / Box blur | ● | ● | ● | ⬜ | P3 |
| 9.3 | Directional / Motion Blur | ● | ● | ● | ⬜ | P2 |
| 9.4 | Radial / Zoom Blur | ● | ◐ | ● | ⬜ | P2 |
| 9.5 | Lens / Camera Lens Blur (bokeh) | ● | ◐ | ● | ⬜ | P2 |
| 9.6 | Compound blur (blur by a map) | ● | ● | ◐ | ⬜ | P3 |
| 9.7 | Bilateral / surface blur | ● | – | ◐ | ⬜ | P3 |
| 9.8 | Channel blur | ● | ● | ◐ | ⬜ | P3 |
| 9.9 | Tilt-shift blur | – | – | ● | ⬜ | P3 |
| 9.10 | Mosaic / pixelate (redaction) | ● | ● | ● | ⬜ | P1 |
| 9.11 | Face / region blur with tracking (redact) | ◐ | ◐ | ● | ⬜ | P1 |
| 9.12 | Background blur by person mask | ◐ | ◐ | ● | ✅ | – |
| 9.13 | Sharpen / Unsharp Mask | ● | ● | ● | ⬜ | P1 |
| 9.14 | Pixel motion blur (from motion vectors) | ● | – | ● | ⬜ | P3 |

## 10. Effects — distort and transform

| # | Effect | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 10.1 | Transform: position, scale, rotation, anchor, skew | ● | ● | ● | 🟡 (no rotation, anchor, skew) | P1 |
| 10.2 | Crop (edges, feather) | ● | ● | ● | 🟡 (layout crop only) | P1 |
| 10.3 | Flip horizontal / vertical, mirror | ● | ● | ● | 🟡 (camera mirror only) | P1 |
| 10.4 | Corner pin / perspective | ● | ● | ● | ⬜ | P2 |
| 10.5 | Mesh / bezier warp, liquify | ● | ◐ | ● (Warper) | ⬜ | P3 |
| 10.6 | Puppet tool | ● | – | ◐ | ⬜ | P3 |
| 10.7 | Bulge, twirl, ripple, wave warp, turbulent displace, displacement map | ● | ● | ● | ⬜ | P3 |
| 10.8 | Spherize, polar coordinates, magnify | ● | ● | ● | ⬜ | P2 (magnify = zoom lens on a region, useful for tutorials) |
| 10.9 | Offset / tile / motion tile / mirror | ● | ● | ● | ⬜ | P3 |
| 10.10 | Lens distortion / optics compensation | ● | ● | ● | ⬜ | P2 |
| 10.11 | Digital zoom / punch-in (Ken Burns) | ◐ | ◐ | ● (Dynamic zoom) | ✅ (zoom) | – |
| 10.12 | Auto reframe (aspect change, follows subject) | – | ● | ● (Smart Reframe) | ⬜ | P1 |
| 10.13 | Cursor-following zoom for screen recordings | – | – | – | ⬜ | P0 (Lectern-specific, competes with Screen Studio/Tella) |
| 10.14 | Drop shadow, rounded corners, border on any layer | ● | ◐ | ◐ | 🟡 (screen/camera style only) | P1 |

## 11. Effects — stylize, light and texture

| # | Effect | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 11.1 | Vignette | ● | ● | ● | ✅ | – |
| 11.2 | Glow / bloom | ● | ● | ● | ⬜ | P1 |
| 11.3 | Film grain / add noise | ● | ● | ● | ⬜ | P1 |
| 11.4 | Halation | – | – | ● | ⬜ | P2 |
| 11.5 | Lens flare / light rays / aperture diffraction | ● | ● | ● | ⬜ | P3 |
| 11.6 | Light leaks / prism / chromatic aberration (stylize) | ◐ | ◐ | ● | ⬜ | P2 |
| 11.7 | Emboss, find edges, posterize, threshold, cartoon, mosaic | ● | ● | ● | ⬜ | P3 |
| 11.8 | Scan lines, VHS, analog damage, TV effect | ◐ | ◐ | ● | ⬜ | P3 |
| 11.9 | Drop shadow, bevel, stroke, inner/outer glow (layer styles) | ● | ◐ | ◐ | ⬜ | P1 |
| 11.10 | Letterbox / blanking fill / aspect matte | ◐ | ◐ | ● | ⬜ | P2 |
| 11.11 | Gradient, 4-color gradient, fill, ramp | ● | ● | ● | 🟡 (2-color background only) | P1 |
| 11.12 | Grid, checkerboard, circle, ellipse, beam, stroke, write-on | ● | ◐ | ● (Fusion) | ⬜ | P2 |
| 11.13 | Fractal noise, cell pattern, caustics, wave world | ● | – | ● (Fusion) | ⬜ | P3 |
| 11.14 | Particles (CC Particle World, Particle Systems II, pEmitter) | ● | – | ● (Fusion) | ⬜ | P3 |
| 11.15 | Simulation (shatter, card dance, foam) | ● | – | ◐ | ⬜ | P3 |
| 11.16 | Sky replacement | – | – | ● | ⬜ | P3 |

## 12. Effects — keying and matte

| # | Effect | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 12.1 | Chroma key (Keylight / Ultra Key / 3D Keyer / Delta Keyer) | ● | ● | ● | ⬜ | P1 (green-screen webcam) |
| 12.2 | Luma key | ● | ● | ● | ⬜ | P2 |
| 12.3 | Difference key, color range, linear color key | ● | ◐ | ● | ⬜ | P3 |
| 12.4 | Spill suppression | ● | ● | ● | ⬜ | P1 |
| 12.5 | Matte choker, refine edge, refine soft matte | ● | ◐ | ● | ⬜ | P2 |
| 12.6 | Track mattes (alpha, luma, inverted) | ● | ● | ● | ⬜ | P1 |
| 12.7 | AI background removal (no green screen) | ● (Roto Brush) | ◐ | ● (Magic Mask) | 🟡 (segmentation used for blur only) | P1 |
| 12.8 | Set matte / garbage matte | ● | ● | ● | ⬜ | P2 |

## 13. Compositing

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 13.1 | Opacity per layer, keyframable | ● | ● | ● | ✅ | – |
| 13.2 | Blend modes: Normal, Darken, Multiply, Color Burn, Linear Burn, Lighten, Screen, Color Dodge, Add, Overlay, Soft Light, Hard Light, Vivid/Linear/Pin Light, Hard Mix, Difference, Exclusion, Subtract, Divide, Hue, Saturation, Color, Luminosity | ● | ● | ● | ⬜ | P1 |
| 13.3 | Masks on any layer (shape, feather, expansion, tracking) | ● | ● | ● | ⬜ | P1 |
| 13.4 | Nested sequences / pre-compositions / compound clips | ● | ● | ● | ⬜ | P2 |
| 13.5 | 3D layers, cameras, lights | ● | – | ● (Fusion) | ⬜ | P3 |
| 13.6 | Motion blur on animated layers (shutter angle) | ● | ◐ | ● | ⬜ | P2 |
| 13.7 | Alpha channel import / export (ProRes 4444, PNG sequence) | ● | ● | ● | ⬜ | P2 |
| 13.8 | Picture-in-picture layouts | ◐ | ◐ | ◐ | ✅ (layouts) | – |

## 14. Transitions

| # | Transition | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 14.1 | Cross dissolve / film dissolve / additive dissolve | ● | ● | ● | ⬜ | P0 (no transitions exist yet) |
| 14.2 | Dip to black / white / color | ● | ● | ● | ⬜ | P0 |
| 14.3 | Push, slide, split, whip | ◐ | ● | ● | ⬜ | P1 |
| 14.4 | Wipe (linear, clock, radial, gradient, iris shapes) | ● | ● | ● | ⬜ | P1 |
| 14.5 | Zoom / smooth cut / morph cut | – | ● (Morph Cut) | ● (Smooth Cut) | ⬜ | P1 (hides jump cuts in talking heads) |
| 14.6 | Blur / glitch / light-leak transitions | ◐ | ◐ | ● | ⬜ | P2 |
| 14.7 | 3D transitions (cube spin, flip, page peel) | ◐ | ● | ● | ⬜ | P3 |
| 14.8 | Layout transitions (animated change between layouts) | – | – | – | ⬜ | P0 (Lectern-specific) |
| 14.9 | Transition alignment (center/start/end on cut), duration, easing | ● | ● | ● | ⬜ | P0 |

## 15. Time effects

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 15.1 | Constant speed change / reverse | ● | ● | ● | ⬜ | P1 |
| 15.2 | Speed ramps / time remapping | ● | ● | ● (retime curve) | ⬜ | P1 |
| 15.3 | Frame blending / optical flow interpolation | ● (Pixel Motion) | ● | ● (Speed Warp) | ⬜ | P2 |
| 15.4 | Freeze frame / hold | ● | ● | ● | ⬜ | P1 |
| 15.5 | Echo, posterize time, time displacement | ● | ● | ◐ | ⬜ | P3 |

## 16. Motion and animation

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 16.1 | Keyframes on every parameter (effects and color too) | ● | ● | ● | 🟡 (transform; color in model only) | P1 |
| 16.2 | Interpolation: linear, hold, bezier, ease in/out, auto bezier | ● | ● | ● | 🟡 | P1 |
| 16.3 | Graph editor (value and speed graphs) | ● | ◐ | ● (Curves editor) | ⬜ | P2 |
| 16.4 | Easing presets (ease, overshoot, bounce, elastic) | ◐ | ◐ | ◐ | ⬜ | P1 |
| 16.5 | Motion paths in the viewer (draw and edit the path) | ● | ◐ | ● | ⬜ | P2 |
| 16.6 | Parenting / null objects | ● | – | ● (Fusion) | ⬜ | P3 |
| 16.7 | Expressions (JavaScript) / Fusion expressions | ● | – | ● | ⬜ | P3 |
| 16.8 | Point / planar tracking to attach text or blur | ● | ◐ | ● | ⬜ | P1 |
| 16.9 | Camera tracking (3D) | ● | – | ● | ⬜ | P3 |
| 16.10 | Animation presets (save/load a set of keyframed effects) | ● | ● | ● | ⬜ | P1 |

## 17. Text, titles and motion graphics

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 17.1 | Text layers with font, size, weight, color, background | ● | ● | ● | ✅ | – |
| 17.2 | Stroke, shadow, multiple fills/strokes per text | ● | ● | ● | ⬜ | P1 |
| 17.3 | Per-character animators (range selectors, wiggle) | ● | – | ● (Text+) | ⬜ | P2 |
| 17.4 | In/out animations library | ● (presets) | ● (MOGRTs) | ● (Fusion titles) | 🟡 (11) | P1 |
| 17.5 | Motion graphics templates (MOGRT) / Fusion macros | ● | ● | ● | ⬜ | P2 |
| 17.6 | Shapes (rect, ellipse, polygon, star, arrows, callouts) | ● | ● | ● | ⬜ | P1 (tutorial callouts and arrows) |
| 17.7 | Lower thirds, captions styles, title safe guides | ● | ● | ● | 🟡 | P1 |
| 17.8 | Animated captions (word-by-word highlight) | ◐ | ● | ● | ⬜ | P1 |
| 17.9 | 3D text / extrusion | ● | – | ● | ⬜ | P3 |
| 17.10 | Keystroke and click visualizations | – | – | – | ⬜ | P1 (Lectern-specific) |

## 18. AI-assisted color and effects

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 18.1 | Auto color / auto balance | ● | ● | ● | ⬜ | P1 |
| 18.2 | Shot match | ● | ● | ● | ⬜ | P1 |
| 18.3 | Person / object mask | ● | ◐ | ● | 🟡 | P1 |
| 18.4 | Face refinement / beauty | – | – | ● | ⬜ | P1 |
| 18.5 | Relight | – | – | ● | ⬜ | P3 |
| 18.6 | Depth map | – | – | ● | ⬜ | P3 |
| 18.7 | Object removal | ● | ● | ● | ⬜ | P2 |
| 18.8 | Upscale (Super Scale) | – | – | ● | ⬜ | P2 |
| 18.9 | Smart reframe | – | ● | ● | ⬜ | P1 |
| 18.10 | Speed Warp / frame interpolation | – | ◐ | ● | ⬜ | P2 |

## 19. Workflow and UI for color/effects

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 19.1 | Effects browser with search, categories, favorites | ● | ● | ● | ⬜ | P1 |
| 19.2 | Drag an effect onto a clip or adjustment layer | ● | ● | ● | ⬜ | P1 |
| 19.3 | Effect controls panel: per-parameter keyframe toggles, reset | ● | ● | ● | 🟡 (Color page fields; per-parameter keyframes pending) | P1 |
| 19.4 | Copy / paste attributes (choose which) | ● | ● | ● | ⬜ | P1 |
| 19.5 | Save presets of effects and grades | ● | ● | ● | 🟡 | P1 |
| 19.6 | Viewer overlays for effect controls (zoom center, mask points, crop) | ● | ● | ● | 🟡 | P1 |
| 19.7 | Dedicated color page / workspace with thumbnails of clips | – | ◐ | ● | ⬜ | P2 |
| 19.8 | Control-surface support (Tangent, DaVinci panels) | – | ◐ | ● | ⬜ | P3 |
| 19.9 | Undo per parameter, merge slider drags | ● | ● | ● | ✅ | – |
| 19.10 | Render queue / background render of effects | ● | ● | ● | 🟡 (export only) | P2 |

---

## 20. End-to-end roadmap

The order is set by dependencies, not by the table order. Each phase ends
with tests (pixel tests against reference images, performance benchmarks) and
a real-time playback check on an M1 (8 GB) machine.

### Phase C0 — Pipeline foundation (P0)
1. GPU compositor on QRhi; preview and export share it (1.1, 1.15).
2. Float textures (RGBA16F) end to end, linear-light blending (1.2, 1.3).
3. Color management: per-clip input space, working space (Rec.709 first, then
   DaVinci Wide Gamut/ACEScct), display transform, output transform (1.4–1.7).
4. Effect registry: one GPU shader + parameter schema per effect; effect stack
   with order, enable, keyframes (1.10, 1.11).
5. Move today's color math, LUTs, blur, vignette, zoom into shaders, with
   pixel tests against the current CPU output.

### Phase C1 — Lumetri parity (P1 color)
Highlights/shadows/whites/blacks, vibrance, contrast pivot, white-balance
picker, auto tone (§2); curves and hue curves (§3); HSL secondary with matte
view (§4.1, 4.5); B&W/tint; looks gallery with thumbnails; before/after
compare; scopes: waveform, parade, vectorscope, histogram (§6). More camera
log conversions (§7.5).

### Phase C2 — Effects essentials (P1 effects)
Transitions (dissolve, dips, push, wipe, smooth cut, layout transitions)
(§14); adjustment layers (1.12); sharpen, noise reduction, glow, grain (§8,
§11); mosaic/pixelate and tracked redaction blur (§9.10–9.11); rotation,
crop, flip, drop shadow and rounded corners on any layer (§10); blend modes
and masks (§13.2–13.3); chroma key with spill suppression (§12.1, 12.4);
speed, freeze frame and speed ramps (§15); cursor-following zoom (10.13).

### Phase C3 — Tracking and AI (P1/P2)
Point and planar tracker driving masks, blur and text (4.8, 16.8);
person/object mask beyond background blur (4.9, 12.7); face refinement and
skin smoothing (8.12); shot match and auto balance (2.24, 18.1); smart
reframe (10.12); stabilizer (8.14).

### Phase C4 — Resolve-style grading (P2)
Grade layers → node graph (serial, parallel, layer mixer), timeline and
group grades, stills gallery, keyframed grades, color warper, HDR wheels,
log wheels, film look creator, grade export as LUT (§5, §7, §2.14–2.15).

### Phase C5 — Motion graphics (P2)
Graph editor, easing presets, motion paths, animation presets, shapes and
callouts, per-character text animators, word-by-word captions, templates
(§16, §17).

### Phase C6 — Specialist (P3, only on demand)
3D layers/cameras, particles, simulation, expressions, OpenFX host, RAW
decode, HDR mastering, control surfaces, relight, depth maps.

### Effort and risk
- Full After Effects or Resolve parity is many engineer-years. These apps
  have 300+ effects each. Lectern should match them on what tutorial and
  talking-head creators use (C0–C3), then grow.
- C0 is the largest risk: everything after it depends on the GPU float
  pipeline. Do not add effects on the 8-bit CPU path that would need
  rewriting.
- Performance budget: 1080p30 timeline with one grade, two effects and a
  transition must play in real time on M1 8 GB; export ≥ 2× real time
  (current CPU export ≈ 2× with no grade).

---

## 21. Count summary

| Section | Items | ✅ | 🟡 | ⬜ |
|---|---|---|---|---|
| 1 Foundation | 17 | 3 | 5 | 9 |
| 2 Primaries | 27 | 6 | 4 | 17 |
| 3 Curves | 10 | 0 | 0 | 10 |
| 4 Secondaries | 16 | 0 | 1 | 15 |
| 5 Structure | 12 | 0 | 1 | 11 |
| 6 Scopes | 10 | 0 | 0 | 10 |
| 7 LUTs | 9 | 2 | 3 | 4 |
| 8 Repair | 15 | 0 | 0 | 15 |
| 9 Blur | 14 | 2 | 0 | 12 |
| 10 Distort | 14 | 1 | 4 | 9 |
| 11 Stylize | 16 | 1 | 1 | 14 |
| 12 Keying | 8 | 0 | 1 | 7 |
| 13 Compositing | 8 | 2 | 0 | 6 |
| 14 Transitions | 9 | 0 | 0 | 9 |
| 15 Time | 5 | 0 | 0 | 5 |
| 16 Motion | 10 | 0 | 2 | 8 |
| 17 Text | 10 | 1 | 2 | 7 |
| 18 AI | 10 | 0 | 1 | 9 |
| 19 Workflow | 10 | 1 | 4 | 5 |
| **Total** | **230** | **16** | **28** | **186** |
