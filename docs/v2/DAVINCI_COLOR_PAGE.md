# DaVinci Resolve Color Page — full breakdown and Lectern plan

Reference: DaVinci Resolve Studio 21, Color page (screenshots supplied by
the user, 2026-10-05). This document describes every area of that page,
every palette and every control, so Lectern can build an equivalent "Color"
workspace. It extends v2/COLOR_EFFECTS_PARITY.md (what the grade can do) and
v2/PRO_INTERFACE_PARITY.md (how the editor is laid out).

Sources: items marked *(screenshot)* are read directly from the supplied
screenshots. Other details are from Resolve 19/20 behaviour and should be
re-checked against the Resolve 21 manual before implementation; Resolve 21
may have renamed or moved some controls.

Marks: ✅ Lectern has it · 🟡 partly · ⬜ missing. Priority P0–P3 as in the
other v2 documents.

---

## 1. Page layout *(screenshot)*

```
┌──────────────┬──────────────────────────────┬──────────────────┐
│ Gallery      │ Viewer                       │ Node editor      │
│ (stills,     │ (zoom 49 %, split/wipe,      │ (Clip / Timeline │
│  PowerGrades)│  transport, loop)            │  node graph)     │
├──────────────┴──────────────────────────────┴──────────────────┤
│ (Clips thumbnails + mini timeline — hidden in the screenshot)  │
├──────── palette bar: 17 left palettes ─────────┬─ keyframes · scopes · info ─┤
│ Active left palette (here: Primaries – Color   │ Right palette (here: Scopes │
│ Wheels)                                        │ – Parade, 0…1023)           │
└────────────────────────────────────────────────┴─────────────────────────────┘
│ Page bar: Resolve logo · 8 page buttons · Home · Settings     │
```

| # | Area | Lectern now | P | Notes |
|---|---|---|---|---|
| 1.1 | Gallery (stills, PowerGrades, memories) — left top | ✅ | – | Screenshot shows "No stills created" |
| 1.2 | Viewer — centre top | 🟡 (Color page viewer) | P0 | Lectern preview exists, no color-page tools |
| 1.3 | Node editor — right top | 🟡 | P2 | Lectern starts with an ordered list of corrector layers (COLOR_EFFECTS_PARITY §5.1) |
| 1.4 | Clips strip (thumbnail per clip, version, codec) + mini timeline | ✅ (clip strip) | P1 | Fast clip-to-clip grading |
| 1.5 | Left palette area (one palette at a time) | ✅ (Primaries palette) | P1 | Adjust panel today |
| 1.6 | Right palette area (Keyframes, Scopes, Info) | 🟡 | P1 |  |
| 1.7 | Page bar along the bottom | ✅ (Edit | Color page bar) | P2 | Lectern: Simple / Pro / Color modes |

### 1.1 Page bar *(screenshot, image 4)*

Left to right: Resolve logo + "DaVinci Resolve Studio 21", then 8 page
buttons, then Home and Settings (gear) at the right. The Color page button is
highlighted with a red underline.

| # | Icon (as seen) | Page | Lectern equivalent |
|---|---|---|---|
| 1.8 | Picture frame | Media | Project panel (PRO_INTERFACE §4) ⬜ |
| 1.9 | Camera with sparkle | New in 21; looks like a photo/camera page. Verify the name in the Resolve 21 manual | – |
| 1.10 | Clapper with cut | Cut | Simple editor (today) 🟡 |
| 1.11 | Timeline bars | Edit | Simple editor timeline 🟡 |
| 1.12 | Wand | Fusion | Pro mode motion graphics ⬜ |
| 1.13 | Color dots (selected) | Color | Color page (Edit · Color page bar) ✅ |
| 1.14 | Music note | Fairlight | Audio panel 🟡 |
| 1.15 | Rocket | Deliver | Export dialog ✅ |
| 1.16 | House | Project manager / home | Home screen ✅ |
| 1.17 | Gear | Project settings | Setup panel 🟡 |

---

## 2. Top toolbar *(screenshot, image 5)*

| # | Control | What it does | Lectern | P |
|---|---|---|---|---|
| 2.1 | Gallery toggle (sidebar icon) | Show/hide the gallery | ✅ | – |
| 2.2 | Import/download icon | Import stills / PowerGrades | ⬜ | P2 |
| 2.3 | Thumbnail size slider | Gallery thumbnail size | ⬜ | P3 |
| 2.4 | Sort, grid view, list view | Gallery display | ⬜ | P3 |
| 2.5 | Search | Find stills | ⬜ | P3 |
| 2.6 | Expand (fullscreen gallery) | | ⬜ | P3 |
| 2.7 | ··· menu | Gallery options | ⬜ | P3 |
| 2.8 | Zoom "49 %" ▾ | Viewer magnification | ⬜ | P1 |
| 2.9 | Split-screen / wipe ▾ | Compare with still, previous clip, versions | ✅ | – |
| 2.10 | Clip name / timecode field ▾ | Current clip, timeline timecode | 🟡 | P1 |
| 2.11 | Highlight toggle (color sparkle icon) | Show the qualifier/window matte on the viewer | ✅ | – |
| 2.12 | ··· menu | Viewer options (unmix, guides, safe area) | ⬜ | P2 |
| 2.13 | Pointer / selection ▾ | Node editor tool | ⬜ | P2 |
| 2.14 | Node layout ▾ | Arrange / clean up nodes | ⬜ | P2 |
| 2.15 | "Clip" ▾ | Which node graph: Clip, Timeline, Group pre-clip, Group post-clip | ⬜ | P1 (clip + timeline first) |
| 2.16 | Node view options ▾, ··· | Node editor settings | ⬜ | P3 |

## 3. Viewer *(screenshot)*

| # | Control | Lectern | P |
|---|---|---|---|
| 3.1 | Picture, scrubber bar under it | ✅ | – |
| 3.2 | Picker ▾ (qualifier pick / add / subtract / feather) | 🟡 | P1 |
| 3.3 | Layers icon (matte / overlay mode) | ⬜ | P2 |
| 3.4 | Audio on/off | 🟡 | P2 |
| 3.5 | Transport: go to first, reverse, stop, play, go to last | 🟡 | P1 |
| 3.6 | Loop | ⬜ | P1 |
| 3.7 | On-screen controls: window shapes, tracker points, qualifier picks | 🟡 | P1 |
| 3.8 | Enhanced viewer (Alt+F) / cinema viewer (⌘F) | ⬜ | P2 |
| 3.9 | Split screen: selected clips, still, previous/next clip, versions | 🟡 | P1 |

## 4. Palette bar *(screenshot, 17 left + 3 right)*

| # | Icon position | Palette | Lectern | P |
|---|---|---|---|---|
| 4.1 | 1 | Camera Raw | ⬜ | P3 |
| 4.2 | 2 | Color Match (chart match) | ⬜ | P2 |
| 4.3 | 3 (selected) | Primaries — Color Wheels / Primaries Bars / Log Wheels | ✅ | P0 |
| 4.4 | 4 | HDR Wheels (zones) | ⬜ | P2 |
| 4.5 | 5 | RGB Mixer | ⬜ | P2 |
| 4.6 | 6 | Motion Effects (temporal/spatial NR, motion blur) | ⬜ | P1 |
| 4.7 | 7 | Curves (Custom, Hue vs Hue/Sat/Lum, Lum vs Sat, Sat vs Sat, Sat vs Lum) | ✅ (Custom YRGB + all six HSL curves) | – |
| 4.8 | 8 | Color Slice (Resolve 20+; drop icon) — verify name in 21 | ⬜ | P2 |
| 4.9 | 9 | Color Warper (hue-sat, chroma-luma mesh) | ⬜ | P3 |
| 4.10 | 10 | Qualifier (HSL, RGB, Luma, 3D) | ✅ | – |
| 4.11 | 11 | Window (power windows) | 🟡 | P1 |
| 4.12 | 12 | Tracker (window, stabilizer, FX) | ⬜ | P1 |
| 4.13 | 13 | Magic Mask (AI person/object) | 🟡 | P1 |
| 4.14 | 14 | Blur (blur, sharpen, mist) | 🟡 | P1 |
| 4.15 | 15 | Key (key input/output gain, offset) | ⬜ | P2 |
| 4.16 | 16 | Sizing (input, output, node, reference sizing) | 🟡 | P1 |
| 4.17 | 17 | Stereo 3D | ⬜ | P3 |
| 4.18 | right 1 | Keyframes (dynamic/static per node) | ⬜ | P1 |
| 4.19 | right 2 | Scopes | ✅ | P1 |
| 4.20 | right 3 | Info (clip metadata, node info) | ⬜ | P3 |

---

## 5. Primaries — Color Wheels *(screenshot, every control)*

Header: "Primaries - Color Wheels", with mode buttons at the right: Color
Wheels (selected), Primaries Bars, Log Wheels, and a reset-all button.

### 5.1 Top row

| # | Control | Default | Range (Resolve) | Lectern | P |
|---|---|---|---|---|---|
| 5.1 | **A** — Auto Balance | – | one-shot | ✅ (A button) | P1 |
| 5.2 | White-balance picker (eyedropper) | – | click a neutral area | ⬜ | P1 |
| 5.3 | Temp | 0.0 | −4000…+4000 | ✅ (temperature, different scale) | – |
| 5.4 | Tint | 0.00 | −100…+100 | ✅ | – |
| 5.5 | Contrast | 1.000 | 0…2 | ✅ (contrast with pivot) | P1 |
| 5.6 | Pivot | 0.435 | 0…1 | ✅ | P1 |
| 5.7 | Mid/Detail | 0.00 | −100…+100 | ⬜ | P1 |

### 5.2 Wheels

Four wheels, each with: a reset arrow; a puck in the color circle (chroma);
four numeric fields **Y R G B** (white, red, green, blue underlines); and a
master jog wheel below (drag left/right for luminance). Lift and Gain also
have a crosshair picker (black point / white point) at their top-left.

| # | Wheel | Default Y R G B | Affects | Lectern | P |
|---|---|---|---|---|---|
| 5.8 | Lift | 0.00 0.00 0.00 0.00 | shadows | ✅ (no YRGB fields, no black picker) | P1 for fields |
| 5.9 | Gamma | 0.00 0.00 0.00 0.00 | midtones | ✅ (same note) | P1 |
| 5.10 | Gain | 1.00 1.00 1.00 1.00 | highlights | ✅ (same note, no white picker) | P1 |
| 5.11 | Offset | 25.00 25.00 25.00 (R G B only) | whole image | ✅ | P1 |
| 5.12 | Master jog wheel under each wheel | – | luminance of that range | ✅ | P1 |
| 5.13 | Black point / white point pickers on Lift / Gain | – | set black/white from the image | ⬜ | P2 |
| 5.14 | Per-wheel reset | – | – | ✅ | – |
| 5.15 | Numeric fields: drag to scrub, double-click to type, double-click label to reset | – | – | 🟡 (scrub fields for the top and bottom rows; the wheels' Y R G B fields are read-only) | P0 (same as scrubby fields in PRO_INTERFACE §5.5) |

### 5.3 Bottom row

| # | Control | Default | Range | Lectern | P |
|---|---|---|---|---|---|
| 5.16 | Color Boost | 0.00 | −100…+100 | ✅ | P1 |
| 5.17 | Shadows | 0.00 | −100…+100 | ✅ | P1 |
| 5.18 | Highlights | 0.00 | −100…+100 | ✅ | P1 |
| 5.19 | Saturation | 50.00 | 0…100 | ✅ (different scale) | – |
| 5.20 | Hue | 50.00 | 0…100 (50 = none) | ✅ | P2 |
| 5.21 | Lum Mix | 100.00 | 0…100 | ⬜ | P2 |

Each bottom-row and top-row field has a small colored indicator bar under it
(rainbow for Color Boost/Saturation/Hue, white for Highlights, dark for
Shadows) — part of the visual language to copy.

### 5.4 Primaries Bars and Log Wheels modes

| # | Mode | Contents | Lectern | P |
|---|---|---|---|---|
| 5.22 | Primaries Bars | Lift/Gamma/Gain/Offset as Y R G B vertical bars | ⬜ | P2 |
| 5.23 | Log Wheels | Shadow / Midtone / Highlight / Offset wheels with Low Range, High Range, Low Soft, High Soft | ⬜ | P2 |
| 5.24 | Page 2 of the palette (·· dots) | Further primaries controls on a second page | ⬜ | P2 |

---

## 6. Scopes *(screenshot)*

Shown: "Scopes" with mode "Parade" ▾, settings, expand, ··· menu; vertical
scale 0, 128, 256 … 1023 (10-bit code values), yellow graticule.

| # | Feature | Lectern | P |
|---|---|---|---|
| 6.1 | Parade (R, G, B side by side) | ✅ | P1 |
| 6.2 | Waveform (luma / RGB / YRGB) | ✅ | P1 |
| 6.3 | Vectorscope (with skin-tone indicator, 75 %/100 % targets) | ✅ | P1 |
| 6.4 | Histogram | ✅ | P1 |
| 6.5 | CIE chromaticity | ⬜ | P3 |
| 6.6 | 1-up / 2-up / 4-up layout | 🟡 | P2 |
| 6.7 | Scale: 10-bit (0–1023), %, HDR nits | 🟡 (10-bit scale) | P1 (10-bit and %) |
| 6.8 | Scope settings: brightness, graticule, color, low-pass filter, extents | ⬜ | P2 |
| 6.9 | Expand to a floating window | ⬜ | P2 |
| 6.10 | Scopes driven by the graded output at full resolution, real time | ⬜ | P1 (GPU compute) |

---

## 7. The other palettes, control by control

### 7.1 Camera Raw (P3)
Decode quality, color space, gamma, white balance, color temp, tint,
exposure, sharpness, highlight recovery, color boost, saturation, contrast,
midtones, lift, gain, per-format tabs (BRAW, ARRI, RED, Sony, Canon, DNG).

### 7.2 Color Match (P2)
Chart type (X-Rite ColorChecker etc.), source gamma, target gamma, target
color space, color temperature, white level, match button; drag the chart
grid onto the viewer.

### 7.3 HDR Wheels (P2)
Zones: Black, Dark, Shadow, Light, Highlight, Specular, Global; each with
exposure, saturation and a color puck; zone falloff; zone editor; temp, tint,
hue, contrast, pivot, MD (mid/detail), black offset.

### 7.4 RGB Mixer (P2)
Red, green, blue output each with R/G/B input sliders; monochrome
checkbox; preserve luminance; swap channels buttons.

### 7.5 Motion Effects (P1 for noise reduction)
Temporal NR: frames (1–5), mo. est. type (faster/better), motion range,
luma/chroma threshold, motion threshold, blend. Spatial NR: mode
(faster/better/enhanced), radius, luma/chroma threshold, blend. Motion
blur: mo. est. type, motion range, motion blur amount.

### 7.6 Curves (P1)
Custom (YRGB with ganged/un-ganged, soft clip lows/highs), Hue vs Hue,
Hue vs Sat, Hue vs Lum, Lum vs Sat, Sat vs Sat, Sat vs Lum; picker to add
points from the viewer; six-vector preset points; intensity per channel.

### 7.7 Color Slice (P2, verify in 21)
Vector-based hue slices (red, skin, yellow, green, cyan, blue, magenta) with
center, hue, saturation, density; global density.

### 7.8 Color Warper (P3)
Hue–Saturation mesh and Chroma–Luma mesh; grid resolution; pin/unpin
points; select/convert tools.

### 7.9 Qualifier (P1)
Modes HSL, RGB, Luma, 3D; per channel low/high/soft/symmetric; matte
finesse: pre-filter, clean black/white, black/white clip, blur radius,
in/out ratio, morph operation, shrink/grow, denoise; invert; highlight view.

### 7.10 Window (P1)
Shapes: linear, circle, polygon, curve (bezier), gradient; plus AI
"object" windows in newer versions; per window: size, aspect, pan, tilt,
rotate, opacity, softness 1–4, inside/outside softness, invert, mask/matte
combine (union, intersect); window presets.

### 7.11 Tracker (P1)
Modes: Window, Stabilizer, FX; track forward/back, by frame; pan, tilt,
zoom, rotate, 3D/perspective; cloud tracker vs. point tracker; interactive
mode; frame/clip mode; keyframe the track.

### 7.12 Magic Mask (P1)
Person vs feature (face, hair, arms, clothing …) vs object mode; strokes
add/subtract; track forward/back; quality better/faster; matte finesse:
smart refine, consistency, shrink/grow, blur, clean black/white.

### 7.13 Blur (P1)
Tabs Blur, Sharpen, Mist; radius, H/V ratio, scaling per R/G/B (linked);
coring softness, level, mix.

### 7.14 Key (P2)
Key input gain/offset, key output gain/offset, qualifier/mask/matte gain;
invert.

### 7.15 Sizing (P1 for input sizing)
Input sizing: zoom, width, height, pan, tilt, rotate, pitch, yaw, flip H/V,
smart reframe; Output sizing; Node sizing; Reference sizing; Image
stabilization.

### 7.16 Stereo 3D (P3)
Convergence, floating windows, auto color/geometry match.

### 7.17 Keyframes (P1, right palette)
Per-node tracks: corrector, sizing, window, etc.; dynamic vs static
keyframes; dissolve type and duration; keyframe all/selected.

### 7.18 Info (P3, right palette)
Clip details (source, codec, resolution, frame rate, data levels),
node info.

---

## 8. Node editor (P2)

| # | Feature | Lectern | P |
|---|---|---|---|
| 8.1 | Serial node (Alt+S), node before (Shift+S) | ✅ | – |
| 8.2 | Parallel node (Alt+P), layer mixer node (Alt+L) | ⬜ | P2 |
| 8.3 | Outside node (Alt+O) — the inverse of a selection | ✅ | – |
| 8.4 | Splitter / combiner, key mixer | ⬜ | P3 |
| 8.5 | Node labels, enable/disable (⌘D), reset node | ✅ | – |
| 8.6 | Node cache, node key input/output | ⬜ | P3 |
| 8.7 | Compound nodes | ⬜ | P3 |
| 8.8 | Color space transform / ResolveFX on a node | ⬜ | P2 |
| 8.9 | Clip / timeline / group pre / group post graphs | 🟡 | P1 (clip + timeline) |
| 8.10 | Shared nodes (one node used by many clips) | ⬜ | P2 |
| 8.11 | Versions (local/remote), copy grade (Shift+=, middle-click) | 🟡 | P1 |

## 9. Gallery (P2)

| # | Feature | Lectern | P |
|---|---|---|---|
| 9.1 | Grab still (⌥⌘G) | ✅ | – |
| 9.2 | Apply grade from still (middle-click / right-click) | ✅ | – |
| 9.3 | Wipe against a still | ✅ | – |
| 9.4 | PowerGrade albums (shared across projects) | 🟡 | P2 |
| 9.5 | Memories (Alt+1…8 to save, Ctrl+1…8 to recall) | ⬜ | P2 |
| 9.6 | Export still as image + .cube / .drx | ⬜ | P2 |
| 9.7 | Lightbox view of all clips | ⬜ | P3 |

---

## 10. Lectern implementation plan for the Color workspace

1. **Data model.** Replace the single `ColorAdjustments` per clip with an
   ordered list of corrector layers (each: primaries, curves, qualifier,
   windows, blur/sharpen, key in/out, enabled, label). Keep the existing
   fields as the first layer so old projects migrate with no visual change.
   Add a timeline-level corrector list. Project format v3.
2. **GPU pipeline** (COLOR_EFFECTS_PARITY Phase C0) — float, linear,
   color-managed; each corrector compiles to one shader pass; qualifier and
   windows produce a matte texture that limits the pass.
3. **Primaries palette** matching §5 exactly: wheels with YRGB fields, master
   jog wheels, Offset wheel, top and bottom rows, Auto balance, pickers,
   Primaries Bars and Log Wheels modes. Same defaults and ranges as Resolve
   so colorists feel at home; map to the internal math.
4. **Scopes** (§6) computed on the GPU from the graded frame: parade,
   waveform, vectorscope, histogram, 10-bit and % scales.
5. **Curves, Qualifier, Window, Tracker, Magic Mask, Blur, Sizing** palettes
   (§7) in that order.
6. **Clips strip + gallery** (grab still, wipe, apply grade).
7. **Node editor** later, on top of the corrector-layer model (layers are a
   serial node chain; parallel and layer-mixer nodes extend it).

Each step ships with pixel tests (grade a known chart, compare to reference
values within tolerance) and QML interaction tests (drag a wheel puck, scrub
a field, pick white balance).

## 11. Count summary

| Section | Items | ✅ | 🟡 | ⬜ |
|---|---|---|---|---|
| 1 Layout (page bar mapping not counted) | 7 | 4 | 3 | 0 |
| 2 Top toolbar | 16 | 3 | 1 | 12 |
| 3 Viewer | 9 | 1 | 5 | 3 |
| 4 Palette bar | 20 | 3 | 5 | 12 |
| 5 Primaries | 24 | 16 | 1 | 7 |
| 6 Scopes | 10 | 4 | 2 | 4 |
| 8 Node editor | 11 | 3 | 2 | 6 |
| 9 Gallery | 7 | 3 | 1 | 3 |
| **Total** | **104** | **37** | **20** | **47** |

Counts recomputed 2026-10-06 (after the Color page rebuild). The gap list against all of Resolve's color
features (pipeline, AI tools, film tools, grade management), and the
comparison with every other grading tool, is in
[COLOR_GRADING_COMPARISON.md](COLOR_GRADING_COMPARISON.md).

Section 7 lists palette controls as prose (no per-row status): all ⬜ except
Magic Mask (person segmentation exists), Blur (blur exists) and Sizing
(zoom/position exist).
