# Pro Interface Parity — After Effects-style workspace, tools and panels

Companion to COLOR_EFFECTS_PARITY.md. That document lists *what* the
pictures can do (color, effects). This one lists *how the user works*: the
workspace, every tool, every panel, the viewer, the timeline switches, the
graph editor, the Character/Paragraph panels, shortcuts and the feel of the
interface. The reference is **Adobe After Effects**, with the matching parts
of **Premiere Pro** and **DaVinci Resolve** where they differ.

Status (2026-10-05): **inventory and roadmap only**. Rows marked ✅ were
checked against the current QML (`src/ui/qml/editor/*`,
`src/ui/qml/screens/EditorScreen.qml`).

Marks are the same as in COLOR_EFFECTS_PARITY.md: ✅ have · 🟡 partly · ⬜
missing; AE/PR/DR ● native · ◐ partly or a different tool · – none; P0–P3
priority (P0 first).

---

## 0. Design principle: two modes, one engine

Lectern's current editor is simple and guided: a tool rail with 9 panels
(Setup, Layout, Cut, Effects, Overlay, Style, Subtitles, Audio, Adjust), a
preview and a timeline. After Effects is dense and expert-oriented. Copying
it wholesale would lose Lectern's ease of use. So:

- **Simple mode** (today's editor) stays the default.
- **Pro mode** is a second workspace with the After Effects-style layout below:
  tools bar, project panel, viewer, effects & presets, effect controls,
  full timeline with switches and graph editor.
- Both modes edit the same project through the same `ProjectController`
  commands, so a project moves freely between them.

---

## 1. Workspace layout (the 7 areas in the reference screenshot)

| # | Area | AE | PR | DR | Lectern | P | Notes |
|---|---|---|---|---|---|---|---|
| 1.1 | ① Menu bar: File, Edit, Composition, Layer, Effect, Animation, View, Window, Help | ● | ● (Sequence, Clip, Markers, Graphics) | ● (Timeline, Clip, Mark, View, Playback, Fusion, Color, Fairlight, Workspace) | ✅ (native macOS menu bar) | P1 | Native macOS menu bar via Qt `MenuBar`; every command also listed there with its shortcut |
| 1.2 | ② Tools bar (selection, hand, zoom, shapes, pen, type, brush, puppet …) | ● | ● (vertical) | ◐ (Edit page toolbar) | ⬜ | P0 | See §2 |
| 1.3 | ③ Workspace switcher (Default, Review, Learn, Small Screen, Standard, Effects, Color, Animation …) | ● | ● | ● (pages) | 🟡 | P1 | Lectern remembers panel sizes and tool; no named workspaces |
| 1.4 | ④ Project panel (media bin) | ● | ● | ● (Media Pool) | ⬜ | P1 | See §4 |
| 1.5 | ⑤ Composition viewer | ● | ● (Program monitor) | ● (Viewer) | 🟡 | P0 | Preview with canvas tools exists; see §3 |
| 1.6 | ⑥ Right column: Info, Audio, Preview, Effects & Presets, Character, Paragraph, Align, Tracker … | ● | ● | ● (Inspector, Effects library) | 🟡 | P1 | Tool panels exist but one at a time |
| 1.7 | ⑦ Timeline with layer outline, switches and keyframes | ● | ● | ● | 🟡 | P0 | See §6 |
| 1.8 | Source monitor (trim a clip before adding) | – | ● | ● | ⬜ | P2 | |
| 1.9 | Effect Controls panel (stacked next to Project) | ● | ● | ● (Inspector) | 🟡 | P0 | See §5 |
| 1.10 | Status bar: frame render time, toggle switches/modes, zoom | ● | ◐ | ◐ | ⬜ | P2 | |

### 1.1 Panel system

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 1.11 | Dock panels in frames, as tabs, or stacked | ● | ● | ◐ | ⬜ | P1 |
| 1.12 | Drag a panel to re-dock (drop-zone highlight) | ● | ● | – | ⬜ | P2 |
| 1.13 | Undock to a floating window | ● | ● | ◐ | ⬜ | P2 |
| 1.14 | Maximize the panel under the pointer (` key) | ● | ● | ◐ | ⬜ | P1 |
| 1.15 | Resize frames by dragging dividers | ● | ● | ● | ✅ | – |
| 1.16 | Save, rename, reset, delete custom workspaces | ● | ● | ● | ⬜ | P2 |
| 1.17 | Panel menu (≡) per panel | ● | ● | ● | ⬜ | P2 |
| 1.18 | Search Help / command palette | ● | ● | ◐ | ⬜ | P1 (command palette with every command and shortcut) |
| 1.19 | Remember layout per project vs. globally | ● | ● | ● | 🟡 (global) | P3 |
| 1.20 | Second monitor / full-screen preview | ● | ● | ● | ⬜ | P2 |

---

## 2. Tools bar (②) — every tool

AE shortcut in brackets. "Lectern" says whether an equivalent exists in the
viewer or timeline today.

| # | Tool | AE | PR | DR | Lectern | P | Notes |
|---|---|---|---|---|---|---|---|
| 2.1 | Home (start screen) | ● | ● | ● | ✅ | – | Home button in the top bar |
| 2.2 | Selection [V] | ● | ● | ● | ✅ | – | Click/drag layers in the viewer, clips in the timeline |
| 2.3 | Hand / pan [H, hold Space] | ● | ● | ● | ⬜ | P1 | Viewer pan when zoomed |
| 2.4 | Zoom [Z] (click in, Alt-click out) | ● | ● | ● | ⬜ | P1 | |
| 2.5 | Camera: Orbit, Pan, Dolly (unified camera) [C] | ● | – | ● (Fusion) | ⬜ | P3 | Needs 3D layers |
| 2.6 | Pan Behind / anchor point [Y] | ● | – | ● | ⬜ | P2 | Needs anchor point in transform |
| 2.7 | Rotation [W] | ● | ◐ | ● | ⬜ | P1 | Rotate handle on the bounding box |
| 2.8 | Rectangle, Rounded Rectangle, Ellipse, Polygon, Star [Q] | ● | ● | ● (Fusion) | ⬜ | P1 | Shape layers; also masks when a layer is selected |
| 2.9 | Pen [G], Add/Delete/Convert Vertex, Mask Feather | ● | ● | ● | ⬜ | P1 | Bezier masks and shapes |
| 2.10 | Horizontal / Vertical Type [Ctrl+T] | ● | ● | ● | 🟡 | P1 | Text clips + in-place editing; no click-to-create-in-viewer, no vertical type |
| 2.11 | Brush [Ctrl+B] | ● | – | ● (Fusion paint) | ⬜ | P3 | Annotation drawing for tutorials is P2 |
| 2.12 | Clone Stamp | ● | – | ● | ⬜ | P3 | |
| 2.13 | Eraser | ● | – | ● | ⬜ | P3 | |
| 2.14 | Roto Brush / Refine Edge [Alt+W] | ● | – | ● (Magic Mask) | 🟡 | P2 | Person segmentation exists; no brush UI |
| 2.15 | Puppet Position / Starch / Bend / Advanced / Overlap [Ctrl+P] | ● | – | ◐ | ⬜ | P3 | |
| 2.16 | Snapping toggle with options (edges, centers, guides) | ● | ● | ● | ✅ | – | Viewer snaps to center/edges; timeline magnet |
| 2.17 | 3D axis mode: Local / World / View | ● | – | ● | ⬜ | P3 | |
| 2.18 | Fill / Stroke swatches and stroke width for shapes | ● | ● | ● | ⬜ | P1 | Shown when a shape tool is active |
| 2.19 | Tool options bar (context options for the active tool) | ● | ◐ | ◐ | ⬜ | P1 | |
| 2.20 | Razor / blade [C in PR, B in DR] | ◐ (split layer) | ● | ● | 🟡 | P1 | Split at playhead (S); no click-to-cut tool |
| 2.21 | Ripple / roll / slip / slide / rate-stretch edit tools | – | ● | ● | ⬜ | P1 | Editing tools; detailed in ../TIMELINE_ENGINE.md |
| 2.22 | Track select forward / backward | – | ● | ● | ⬜ | P2 | |
| 2.23 | Annotation / callout tools (arrows, highlight box, spotlight) | – | – | – | ⬜ | P1 | Lectern-specific for tutorials |

---

## 3. Composition viewer (⑤)

| # | Control | AE | PR | DR | Lectern | P | Notes |
|---|---|---|---|---|---|---|---|
| 3.1 | Fit to window | ● | ● | ● | ✅ | – | |
| 3.2 | Magnification (12.5–1600%, Fit, Fit up to 100%) | ● | ● | ● | ⬜ | P1 | |
| 3.3 | Preview resolution (Full, Half, Third, Quarter, Auto) | ● | ● | ● | 🟡 | P1 | Renders at the displayed size; no manual choice |
| 3.4 | Region of interest | ● | – | – | ⬜ | P3 | |
| 3.5 | Transparency grid | ● | ● | ● | ⬜ | P2 | |
| 3.6 | Show mask and shape paths toggle | ● | ◐ | ● | ⬜ | P1 | With §2.9 |
| 3.7 | Grid, proportional grid, guides, rulers | ● | ● | ● | ⬜ | P1 | |
| 3.8 | Title/action safe margins | ● | ● | ● | ⬜ | P1 | Plus platform safe zones (YouTube Shorts, TikTok, Reels UI overlays) |
| 3.9 | Show channel: RGB, Red, Green, Blue, Alpha, colorized | ● | ◐ | ● | ⬜ | P2 | |
| 3.10 | Exposure adjust of the viewer (not the render) | ● | – | ◐ | ⬜ | P3 | |
| 3.11 | Take / show snapshot (compare) | ● | ◐ | ● (grab still, wipe) | ⬜ | P1 | |
| 3.12 | Fast previews / adaptive resolution while scrubbing | ● | ● | ● | ⬜ | P1 | Ties to GPU pipeline |
| 3.13 | Current time field (click to type a time) | ● | ● | ● | 🟡 | P1 | Shown, not editable |
| 3.14 | Viewer color management toggle | ● | ● | ● | ⬜ | P2 | |
| 3.15 | 3D views: Active Camera, Front, Left, Top, Custom; 1/2/4-view layouts | ● | – | ● (Fusion) | ⬜ | P3 | |
| 3.16 | Bounding box with corner/edge handles | ● | ● | ● | ✅ | – | |
| 3.17 | Rotation handle, anchor point handle | ● | ● | ● | ⬜ | P1 | |
| 3.18 | Shift-constrain, Alt-from-center, ⌘ no-snap while dragging | ● | ● | ● | 🟡 | P1 | ⌘ free placement exists; Shift/Alt modifiers missing |
| 3.19 | Nudge with arrow keys (1 px, Shift 10 px) | ● | ● | ● | ⬜ | P1 | |
| 3.20 | Esc cancels a drag | ◐ | ◐ | ◐ | ✅ | – | |
| 3.21 | Double-click text to edit in place | ● | ● | ● | ✅ | – | |
| 3.22 | Right-click context menu on layers | ● | ● | ● | ✅ (plus timeline clip/lane/ruler menus) | – | Reset, delete, edit text |
| 3.23 | Click-through layer picking (Alt-click selects the layer below) | ◐ | – | – | ⬜ | P2 | |
| 3.24 | Motion path display with keyframe points | ● | ◐ | ● | ⬜ | P2 | |
| 3.25 | Effect on-viewer controls (zoom center, crop, mask points, tracker points) | ● | ● | ● | 🟡 | P1 | |
| 3.26 | Playback controls: first, previous frame, play, next frame, last; loop; play range | ● (Preview panel) | ● | ● | 🟡 | P1 | Play/step exist; no loop, J-K-L, range |

---

## 4. Project panel (④)

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 4.1 | List of all media, compositions and solids | ● | ● | ● | ⬜ | P1 |
| 4.2 | Thumbnail + info of the selected item (size, duration, fps, color depth) | ● | ● | ● | ⬜ | P1 |
| 4.3 | Columns: name, label, type, size, duration, fps, file path, comment | ● | ● | ● | ⬜ | P2 |
| 4.4 | Search / filter | ● | ● | ● | ⬜ | P1 |
| 4.5 | Folders / bins, smart bins | ● | ● | ● | ⬜ | P2 |
| 4.6 | Import (file, folder, image sequence) and drag in from Finder | ● | ● | ● | 🟡 | P1 |
| 4.7 | Drag an item into the viewer or timeline to add a layer | ● | ● | ● | ⬜ | P1 |
| 4.8 | Interpret footage (fps, alpha, color space, loop) | ● | ● | ● | ⬜ | P2 |
| 4.9 | Replace footage / relink missing media | ● | ● | ● | ⬜ | P1 |
| 4.10 | Find in timeline / reveal in Finder | ● | ● | ● | ⬜ | P2 |
| 4.11 | Label colors | ● | ● | ● | ⬜ | P3 |
| 4.12 | New: Composition / Solid / Adjustment layer / Null / Text / Shape | ● | ● | ● | 🟡 | P1 | "+ Layer" adds text, media, sound, caption, empty layers |

---

## 5. Effect Controls panel

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 5.1 | Every effect of the selected layer, in order, collapsible | ● | ● | ● | 🟡 | P0 | Fixed sections per panel today |
| 5.2 | fx toggle per effect (bypass), Reset, About | ● | ● | ● | 🟡 | P1 | Enable toggles for the 4 effects |
| 5.3 | Drag to reorder effects | ● | ● | ● | ⬜ | P1 |
| 5.4 | Stopwatch per parameter (start keyframing), keyframe navigator ◀ ◆ ▶ | ● | ● | ● | 🟡 | P0 | Keyframes section for transform only |
| 5.5 | Scrubby number fields (drag on the value) and click-to-type | ● | ● | ● | 🟡 (scrub fields on the Color page) | P0 | Core pro feel |
| 5.6 | Angle dial, color swatch + eyedropper, point picker (crosshair), checkbox, popup, curve | ● | ● | ● | 🟡 | P1 |
| 5.7 | Copy / paste effects, save as animation preset | ● | ● | ● | ⬜ | P1 |
| 5.8 | Expression field per parameter (Alt-click stopwatch) | ● | – | ● | ⬜ | P3 |
| 5.9 | Properties panel (AE 2024+): context properties of the selected layer in one place | ● | ● (Properties panel 2025) | ● (Inspector) | 🟡 | P1 |

---

## 6. Timeline (⑦)

### 6.1 Header and navigation

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 6.1 | Current time display (click to type; frames/timecode toggle) | ● | ● | ● | 🟡 | P1 | Display only |
| 6.2 | Layer search field | ● | – | – | ⬜ | P2 |
| 6.3 | Composition mini-flowchart / nesting breadcrumbs | ● | ◐ | ● | ⬜ | P3 |
| 6.4 | Draft 3D, Shy toggle, Frame blending toggle, Motion blur toggle, Graph editor toggle | ● | – | ◐ | ⬜ | P2 |
| 6.5 | Time ruler with current-time indicator (drag to scrub) | ● | ● | ● | ✅ | – |
| 6.6 | Work area / in-out range bar (render and preview range) | ● | ● | ● | 🟡 | P1 | I/O marks remove a section; no preview range |
| 6.7 | Time navigator (zoomed region handles) | ● | ● (zoom scroll bar) | ● | 🟡 | P2 | Zoom slider + scrollbar |
| 6.8 | Zoom: slider, ⌘ wheel, pinch, = / − keys, fit | ● | ● | ● | 🟡 | P1 | No = / − keys |
| 6.9 | Snapping (Shift while dragging in AE) | ● | ● | ● | ✅ | – |
| 6.10 | Comp markers and layer markers with comments, duration markers | ● | ● | ● | 🟡 | P1 | Markers with labels; no durations or layer markers |
| 6.11 | Auto-scroll during playback, edge scroll while dragging | ● | ● | ● | ✅ | – |

### 6.2 Layer outline (left side)

| # | Column / switch | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 6.12 | Video eye (hide), Audio speaker (mute), Solo, Lock | ● | ● | ● | ✅ | – |
| 6.13 | Label color | ● | ● | ● | ⬜ | P3 |
| 6.14 | Layer number | ● | – | – | 🟡 | – | V1/T1/A1 labels |
| 6.15 | Source name / layer name, rename (Enter) | ● | ● | ● | ✅ | – | Double-click a header |
| 6.16 | Shy layer switch | ● | – | – | ⬜ | P3 |
| 6.17 | Collapse transformations / continuously rasterize | ● | – | – | ⬜ | P3 |
| 6.18 | Quality and sampling (draft/best, bilinear/bicubic) | ● | ◐ | ◐ | ⬜ | P3 |
| 6.19 | Effect switch (fx on/off for the layer) | ● | ● | ● | ⬜ | P1 |
| 6.20 | Frame blending switch | ● | ● | ● | ⬜ | P3 |
| 6.21 | Motion blur switch | ● | – | ◐ | ⬜ | P2 |
| 6.22 | Adjustment layer switch | ● | ● | ● | ⬜ | P1 |
| 6.23 | 3D layer switch | ● | – | ● | ⬜ | P3 |
| 6.24 | Modes column: blend mode, preserve transparency (T), track matte (alpha/luma, inverted) | ● | ● | ● | ⬜ | P1 |
| 6.25 | Parent & link column (pick whip) | ● | – | ◐ | ⬜ | P3 |
| 6.26 | In, Out, Duration, Stretch columns | ● | ◐ | ◐ | ⬜ | P2 |
| 6.27 | Toggle Switches / Modes button | ● | – | – | ⬜ | P2 |
| 6.28 | Layer selection highlight; select layer by clicking the header | ● | ● | ● | ✅ | – |
| 6.29 | Reorder layers by dragging (stacking order) | ● | ● | ● | 🟡 | P1 | Up/down buttons and menu; no drag |
| 6.30 | Add, delete, rename layers / tracks | ● | ● | ● | ✅ | – |
| 6.31 | Track height per track, collapse/expand all | ● | ● | ● | 🟡 | P2 | Global height only |

### 6.3 Properties and keyframes in the timeline

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 6.32 | Twirl-down properties under each layer (Transform, Effects, Masks, Text) | ● | ◐ (in Effect Controls) | ● (Keyframes panel) | ⬜ | P0 |
| 6.33 | Solo properties with keys: P, S, R, T, A, U (animated), UU (changed), E (effects), M (masks) | ● | – | – | ⬜ | P1 |
| 6.34 | Keyframe diamonds on property rows; drag to retime, box-select, copy/paste | ● | ● | ● | ⬜ | P0 |
| 6.35 | Keyframe shapes by interpolation (diamond linear, square hold, circle auto-bezier, hourglass ease) | ● | ● | ● | ⬜ | P1 |
| 6.36 | Keyframe navigator per row (◀ ◆ ▶) | ● | ● | ● | 🟡 | P1 |
| 6.37 | Easy Ease (F9), Ease In (Shift+F9), Ease Out (Ctrl+Shift+F9) | ● | ◐ | ◐ | ⬜ | P1 |
| 6.38 | Toggle hold keyframe, keyframe velocity dialog | ● | ◐ | ◐ | ⬜ | P2 |
| 6.39 | Expressions under properties, pick whip | ● | – | ● | ⬜ | P3 |
| 6.40 | Audio waveform under a layer (LL) | ● | ● | ● | ✅ | – |

### 6.4 Layer editing in the timeline

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 6.41 | Drag layer bar to move in time | ● | ● | ● | ✅ | – |
| 6.42 | Trim layer in/out by dragging ends; [ and ] / Alt+[ ] | ● | ● | ● | 🟡 | P1 | Drag only |
| 6.43 | Split layer (Ctrl+Shift+D) | ● | ● | ● | ✅ | – | S |
| 6.44 | Move a clip to another layer by dragging vertically | ● | ● | ● | ✅ | – |
| 6.45 | Box select, Shift/⌘ add to selection | ● | ● | ● | ✅ | – |
| 6.46 | Pre-compose / nest / compound clip | ● | ● | ● | ⬜ | P2 |
| 6.47 | Sequence layers / auto-arrange with overlap | ● | ◐ | ◐ | ⬜ | P3 |
| 6.48 | Time-reverse, time-stretch, time remap on a layer | ● | ● | ● | ⬜ | P1 |
| 6.49 | Duplicate layer (⌘D), copy/paste layers | ● | ● | ● | ⬜ | P1 |
| 6.50 | Lift / extract, ripple delete | ◐ | ● | ● | 🟡 | P1 | Track-only or all-tracks delete |

---

## 7. Graph Editor

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 7.1 | Value graph | ● | ◐ | ● | ⬜ | P2 |
| 7.2 | Speed graph (velocity, influence handles) | ● | ◐ | ● | ⬜ | P2 |
| 7.3 | Reference graph (other properties faded) | ● | – | ◐ | ⬜ | P3 |
| 7.4 | Show selected / animated / graph set properties | ● | – | ● | ⬜ | P3 |
| 7.5 | Auto-zoom graph height, fit selection, fit all | ● | – | ● | ⬜ | P2 |
| 7.6 | Bezier handles: drag to shape the curve, break handles (Alt) | ● | ● | ● | ⬜ | P2 |
| 7.7 | Transform box for many keyframes (scale in time/value) | ● | – | ◐ | ⬜ | P3 |
| 7.8 | Separate dimensions (X/Y curves) | ● | – | ● | ⬜ | P2 |
| 7.9 | Interpolation buttons: hold, linear, auto bezier; easy ease buttons | ● | ● | ● | ⬜ | P2 |
| 7.10 | Snap keyframes, show layer in/out, show audio waveforms in graph | ● | – | ◐ | ⬜ | P3 |
| 7.11 | Easing presets panel (curve thumbnails like the reference image) | ◐ (plug-ins) | ◐ | ◐ | ⬜ | P1 | Simpler than a full graph editor; ship first |

---

## 8. Character and Paragraph panels

| # | Control | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 8.1 | Font family with preview, font style | ● | ● | ● | 🟡 | P1 | Weight only |
| 8.2 | Font search, favorites, recently used, Adobe Fonts / Google Fonts | ● | ● | ◐ | ⬜ | P2 |
| 8.3 | Fill color, stroke color, swap, no fill/no stroke, eyedropper | ● | ● | ● | 🟡 | P1 | Fill + background color |
| 8.4 | Font size | ● | ● | ● | ✅ | – |
| 8.5 | Leading (line height) | ● | ● | ● | ⬜ | P1 |
| 8.6 | Kerning (metrics/optical) and tracking (letter spacing) | ● | ● | ● | ⬜ | P1 |
| 8.7 | Stroke width, stroke over fill / fill over stroke, line join | ● | ● | ● | ⬜ | P1 |
| 8.8 | Vertical / horizontal scale, baseline shift, tsume | ● | ● | ◐ | ⬜ | P3 |
| 8.9 | Faux bold, faux italic, all caps, small caps, superscript, subscript | ● | ● | ◐ | ⬜ | P2 |
| 8.10 | Paragraph align: left, center, right, justify (last left/center/right/all) | ● | ● | ● | 🟡 | P1 | Left/center/right |
| 8.11 | Indents, space before/after paragraph | ● | ● | ◐ | ⬜ | P2 |
| 8.12 | Text box (paragraph text with wrapping) vs point text | ● | ● | ● | ⬜ | P1 |
| 8.13 | Background box / rounded pill with padding | ◐ | ● | ● | ✅ | – |
| 8.14 | Text shadow | ● | ● | ● | ⬜ | P1 |
| 8.15 | Emoji and right-to-left text | ● | ● | ● | ⬜ | P2 |

---

## 9. Other panels (right column ⑥)

| # | Panel | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 9.1 | Effects & Presets: search, categories (3D Channel, Audio, Blur & Sharpen, Channel, Color Correction, Distort, Expression Controls, Generate, Immersive Video, Keying, Matte, Noise & Grain, Perspective, Simulation, Stylize, Text, Time, Transition, Utility), animation presets, favorites | ● | ● | ● | ⬜ | P0 |
| 9.2 | Info (RGB/alpha under cursor, X/Y, layer info) | ● | – | ◐ | ⬜ | P3 |
| 9.3 | Audio (levels meter, volume) | ● | ● | ● | ✅ | – | Level meter + Audio panel |
| 9.4 | Preview (play controls, loop, frame rate, skip, resolution, full screen, cache before playback) | ● | ◐ | ◐ | 🟡 | P1 |
| 9.5 | Align (align layers to each other or the composition, distribute) | ● | ● | ● | ⬜ | P1 |
| 9.6 | Tracker (track motion, stabilize, track camera, warp stabilizer) | ● | ◐ | ● | ⬜ | P1 |
| 9.7 | Essential Graphics / properties for templates | ● | ● | ◐ | ⬜ | P2 |
| 9.8 | Content-Aware Fill | ● | – | ◐ | ⬜ | P3 |
| 9.9 | Paint, Brushes | ● | – | ● | ⬜ | P3 |
| 9.10 | Wiggler, Smoother, Motion Sketch | ● | – | – | ⬜ | P3 |
| 9.11 | Mask Interpolation | ● | – | – | ⬜ | P3 |
| 9.12 | Lumetri Color and Lumetri Scopes | ● | ● | ● (Color page) | 🟡 | P1 | Adjust panel; scopes missing |
| 9.13 | Libraries / asset browser (stock, templates) | ● | ● | ◐ | ⬜ | P3 |
| 9.14 | Undo history panel | ● | ● | ● | ⬜ | P2 | Undo labels already exist in ProjectController |
| 9.15 | Render Queue / Export settings | ● | ● | ● (Deliver) | ✅ | – | Export dialog |
| 9.16 | Layer properties bar on solid/shape: blend mode popup + opacity chip ("Normal · Opacity 100%") | ● | ◐ | ◐ | ⬜ | P1 |
| 9.17 | Color picker dialog: HSB/RGB/hex, swatches, eyedropper anywhere on screen | ● | ● | ● | 🟡 | P1 | Swatches only |

---

## 10. Menus (①) — commands to expose

| Menu | Key commands (AE) | Lectern today |
|---|---|---|
| File | New project / composition, open, recent, save, save as, increment & save, import, dependencies (collect files), export | open/save/export exist (no menu) |
| Edit | Undo/redo (with labels), history, cut/copy/paste, duplicate, split layer, select all, deselect, purge cache, preferences, keyboard shortcuts | undo/redo, select all, split |
| Composition | New, settings (size, fps, duration, background), crop to region, trim to work area, add to render queue, save frame as | Setup panel (aspect, fps, background) |
| Layer | New (text, solid, light, camera, null, shape, adjustment), layer settings, open layer, mask, transform, time, frame blending, 3D, blending mode, track matte, pre-compose, layer styles | partial via "+ Layer" |
| Effect | Last effect, remove all, categories | Effects panel (4 effects) |
| Animation | Add keyframe, keyframe interpolation/velocity/assistant (easy ease), animate text, track motion, warp stabilizer, reveal properties | partial (keyframes section) |
| View | Zoom in/out, resolution, rulers, guides, grids, safe margins, snapping, show layer controls | fit only |
| Window | Workspaces, every panel by name | – |
| Help | Help, shortcuts reference, what's new | – |

---

## 11. Keyboard shortcuts

Lectern today: Space play, S split, ⌫ delete, M marker, ←/→ frame step
(Shift ±1 s), Home/End, I/O in/out, Esc deselect, ⌘Z/⇧⌘Z undo/redo,
⌘E export, ⌘S save, ⌘A select all.

To add (AE/PR conventions, P1 unless noted):

| Keys | Action |
|---|---|
| V, H, Z, W, Q, G, ⌘T, Y | Tools (§2) |
| J / K / L | Play backward / stop / play forward, repeated = faster (PR/DR) |
| ⌘K or ⇧⌘D | Split at playhead (PR / AE) |
| [ / ] , ⌥[ / ⌥] | Move layer start/end to playhead; trim in/out to playhead |
| PageUp / PageDown | Previous / next edit point or keyframe |
| J / K (AE meaning) | Previous / next keyframe (conflicts with PR's JKL — choose per keymap preset) |
| = / − | Zoom timeline in / out; ⇧= viewer |
| ` (backtick) | Maximize panel |
| ⌘D | Duplicate |
| F9 | Easy ease |
| P, S, R, T, A, U, UU, E | Reveal properties (P2) |
| B / N | Work area begin / end (P2) |
| ⌘⇧C | Pre-compose / nest (P2) |
| Arrows / ⇧Arrows | Nudge selected layer 1 / 10 px in the viewer |
| ⌥-scroll, ⇧-scroll | Zoom / horizontal scroll |
| Keymap presets | Lectern, After Effects, Premiere Pro, DaVinci Resolve; editable keyboard map (P2) |

---

## 12. Visual language and feel

From the reference images (dark, dense, icon-driven panels):

| # | Item | Lectern | P | Notes |
|---|---|---|---|---|
| 12.1 | Dark UI, one accent color, compact density | ✅ | – | Theme.qml: periwinkle accent |
| 12.2 | Consistent monochrome icon set for every tool (as in the reference grid: selection, hand, zoom, orbit, pen family, type, brush, stamp, eraser, roto, puppet, shapes, stopwatch, eye, speaker, lock, fx, keyframe diamond) | 🟡 | P1 | ~60 SVG icons exist; ~40 more needed |
| 12.3 | Tooltips with name + shortcut on every control | 🟡 | P1 | |
| 12.4 | Scrubby sliders and hot text (drag numbers) | ⬜ | P0 | |
| 12.5 | Hover, pressed and selected states on every control | ✅ | – | |
| 12.6 | Density option (compact / comfortable) and UI scale | ⬜ | P2 | |
| 12.7 | Light theme | ⬜ | P3 | |
| 12.8 | Smooth 60 fps interactions (no blocking on the UI thread) | 🟡 | P0 | Timeline scrub/drag smoothed (2026-10-05); viewer still CPU-rendered |
| 12.9 | Empty states and onboarding hints in each panel | 🟡 | P2 | |
| 12.10 | Accessible: keyboard focus order, screen-reader names | ⬜ | P2 | |

---

## 13. Premiere Pro and DaVinci Resolve interface items not covered above

| # | Feature | PR | DR | Lectern | P |
|---|---|---|---|---|---|
| 13.1 | Page tabs: Media, Cut, Edit, Fusion, Color, Fairlight, Deliver | – | ● | ⬜ | P2 (Simple/Pro/Color modes instead) |
| 13.2 | Color page: clip thumbnails strip, node editor, gallery, scopes, curves/qualifier/window palettes | ◐ | ● | ⬜ | P2 |
| 13.3 | Lumetri panel sections: Basic, Creative, Curves, Color Wheels & Match, HSL Secondary, Vignette | ● | – | 🟡 | P1 |
| 13.4 | Essential Graphics (browse/edit templates) | ● | – | ⬜ | P2 |
| 13.5 | Text-based editing (edit by transcript) | ● | ● | ⬜ | P1 (fits screen recordings) |
| 13.6 | Source/program dual monitors, three-point editing | ● | ● | ⬜ | P2 |
| 13.7 | Cut page: source tape, dual timeline, sync bin | – | ● | ⬜ | P3 |
| 13.8 | Inspector with tabs (Video, Audio, Effects, Transition, Image, File) | ◐ | ● | 🟡 | P1 |
| 13.9 | Keyframe editor panel and curve editor docked under the timeline | ◐ | ● | ⬜ | P2 |
| 13.10 | Clip attributes / flags / colors, render in place | ● | ● | ⬜ | P3 |

---

## 14. Roadmap (interface)

Runs alongside the color/effects phases in COLOR_EFFECTS_PARITY.md §20.

### Phase U0 — Pro-feel basics (P0, applies to both modes)
Scrubby number fields with click-to-type (§5.5, 12.4); Effect Controls panel
listing the effect stack with fx toggles and per-parameter stopwatch and
keyframe navigator (§5.1–5.4); Effects & Presets browser with search and
categories, drag onto a clip (§9.1); keyframes shown on clip rows in the
timeline, draggable (§6.32, 6.34); tools bar with Selection, Hand, Zoom,
Rotation, Shape, Pen, Type, Razor (§2).

### Phase U1 — Pro workspace (P1)
Pro mode layout with docked panels (Project, Viewer, Effect Controls,
Effects & Presets, Timeline) and a workspace switcher; native menu bar with
every command; command palette; viewer magnification, guides, rulers, safe
margins, snapshot compare; Align panel; easing presets; blend mode and
track matte columns; adjustment layers; full Character/Paragraph panel;
J-K-L, = / −, [ ], ⌘D and nudge shortcuts; drag to reorder layers;
undo history panel.

### Phase U2 — Animation depth (P2)
Graph editor (value/speed, bezier handles, separate dimensions); motion paths
in the viewer; layer and duration markers; nesting/pre-compose; custom
workspaces and floating panels; keymap presets (AE/PR/DR); tracker panel UI.

### Phase U3 — Specialist (P3)
3D views and camera tools, puppet tools, paint/clone/eraser, expressions and
pick whip, wiggler/smoother/motion sketch, parenting, light theme.

### Acceptance for each phase
- Every new control is reachable by mouse, by keyboard shortcut and from the
  menu bar or command palette.
- Each interaction has a QML test with real input events (pattern:
  `tests/qml/TimelineInteractionTest.cpp`, `CanvasInteractionTest.cpp`).
- No interaction drops below 60 fps on an M1 8 GB with a 10-minute 1080p
  project.

---

## 15. Count summary

| Section | Items | ✅ | 🟡 | ⬜ |
|---|---|---|---|---|
| 1 Workspace & panels | 20 | 1 | 6 | 13 |
| 2 Tools | 23 | 3 | 3 | 17 |
| 3 Viewer | 26 | 5 | 5 | 16 |
| 4 Project panel | 12 | 0 | 2 | 10 |
| 5 Effect Controls | 9 | 0 | 5 | 4 |
| 6 Timeline | 50 | 12 | 11 | 27 |
| 7 Graph Editor | 11 | 0 | 0 | 11 |
| 8 Character/Paragraph | 15 | 2 | 3 | 10 |
| 9 Other panels | 17 | 2 | 3 | 12 |
| 12 Visual language | 10 | 2 | 4 | 4 |
| 13 PR/DR extras | 10 | 0 | 2 | 8 |
| **Total** | **203** | **27** | **44** | **132** |
