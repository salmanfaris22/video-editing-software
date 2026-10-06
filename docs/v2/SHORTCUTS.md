# Lectern Keyboard Shortcuts — current, planned, and reference keymaps

macOS keys shown (⌘ Command, ⌥ Option, ⇧ Shift, ⌃ Control). On Windows,
⌘ → Ctrl and ⌥ → Alt unless noted.

Part A is what Lectern has today (checked in `EditorScreen.qml`,
`TimelinePanel.qml`, `CanvasOverlay.qml`). Part B is the planned Lectern
keymap. Part C lists the After Effects, Premiere Pro, DaVinci Resolve and
Final Cut Pro shortcuts that Lectern's optional keymap presets will
reproduce. Part C is from general product knowledge of current versions;
each vendor lets users print the live list (Adobe: *Edit → Keyboard
Shortcuts*; Resolve: *DaVinci Resolve → Keyboard Customization*; Final Cut:
*Final Cut Pro → Commands → Customize*) — verify against those before
shipping a preset. Rows marked *(verify)* are ones most likely to differ by
version.

---

## A. Lectern today

### A.1 Editor

| Keys | Action |
|---|---|
| Space | Play / pause |
| S | Split at the playhead (selected clip, or every recording track) |
| ⌫ / Delete | Delete selection (Cut mode: this track leaves a gap; all tracks closes it) |
| M | Add marker |
| ← / → | Previous / next frame |
| ⇧← / ⇧→ | Back / forward 1 second |
| Home / End | Go to start / end |
| I / O | Mark in / out (section to remove) |
| Esc | Deselect, clear in/out |
| ⌘Z / ⇧⌘Z | Undo / redo |
| ⌘A | Select all clips |
| ⌘S | Save |
| ⌘E | Export |

### A.2 Timeline mouse

| Gesture | Action |
|---|---|
| Drag on the ruler or the playhead | Scrub (frame-accurate, snaps) |
| ⌘ while dragging | No snapping |
| Drag a clip | Move (up/down = onto another layer of the same kind) |
| Drag a clip edge | Trim |
| Drag on empty lane | Box select (Select: Drag mode) |
| ⇧ / ⌘ click, ⇧ / ⌘ box | Add to selection; ⌥ box removes |
| Wheel / trackpad | Scroll (vertical wheel scrolls time when all layers fit) |
| ⇧ wheel | Horizontal scroll |
| ⌘ wheel, pinch | Zoom around the pointer |
| Click / double-click a layer header | Select layer / rename |
| Right-click a layer header | Rename, move up/down, delete |

### A.3 Canvas (preview)

| Gesture | Action |
|---|---|
| Click | Select the top layer under the pointer |
| Drag | Move (snaps to center and edges) |
| ⌘ drag | Move without snapping |
| Drag a corner / edge handle | Resize (corners keep proportions) |
| Esc during a drag | Cancel, layer goes back |
| Double-click text | Edit in place |
| Right-click | Layer menu (edit text, reset, delete) |

### A.4 Color page (DaVinci Resolve style)

| Keys / gesture | Action |
|---|---|
| ⇧6 / ⇧4 | Color page / Edit page |
| ⌘C / ⌘V | Copy the clip's grade / paste it on the selected clips |
| = | Take the grade of the previous clip |
| ⌥S | Add a serial node after the last one |
| ⇧D | Bypass all grades (before / after) |
| ⌘W | Wipe before / after (drag the divider) |
| ⇧H | Highlight what the selected node changes |
| ⌥⌘G | Grab a still into the Gallery |
| ↑ / ↓ | Previous / next clip in the strip |
| Hover a look | Preview it in the viewer; click applies |
| Double-click a still | Apply its grade to the clip |
| Window on the viewer | Drag inside to move, edge handles to resize, the top dot to rotate (⇧ fine) |
| Qualifier → Pick | Click a color in the viewer to key it (e.g. only the pen) |
| Curves → Hue vs … → Pick | Click a color in the viewer to add its point on the HSL curve |
| Timeline ⌥-drag | Move only that clip of a recording (it leaves the recording's link) |
| Right-click a clip / node | Grade copy, previous / next / all, grab still · enable, reset, invert, reorder, delete |

---

## B. Planned Lectern keymap

Lectern default keeps A unchanged and adds the following, chosen to match
Premiere Pro and Resolve where they agree.

### B.1 Playback and navigation

| Keys | Action |
|---|---|
| J / K / L | Play backward / stop / play forward (press again = faster) |
| K + J / K + L | Step one frame back / forward |
| ↑ / ↓ | Previous / next edit point |
| ⇧↑ / ⇧↓ | Previous / next marker |
| ; / ' | Previous / next keyframe on the selected layer |
| ⌥L | Loop playback on/off |
| ⌘⇧F | Full-screen preview |
| Click the timecode, or ⌃G | Type a time to go to |

### B.2 Editing

| Keys | Action |
|---|---|
| ⌘K | Split at playhead (alias of S) |
| ⇧⌘K | Split every track at playhead |
| ⇧⌫ | Ripple delete (always closes the gap) |
| Q / W | Ripple trim the start / end of the clip under the playhead to the playhead |
| [ / ] | Move the selected clip's start / end to the playhead |
| ⌥[ / ⌥] | Trim the selected clip's start / end to the playhead |
| ⌘D | Duplicate |
| ⌘C / ⌘X / ⌘V | Copy / cut / paste at playhead |
| ⌥⌘V | Paste attributes (effects, color, transform) |
| ⌘R | Speed / duration |
| ⌥⇧F | Freeze frame |
| ⌘L | Link / unlink |
| ⇧E | Enable / disable clip |
| N | Snapping on/off |
| ⌘T | Add default transition at the edit point |
| ⇧I / ⇧O | Go to in / out |
| ⌥X | Clear in and out |
| ⌘G / ⇧⌘G | Group / nest selection; ungroup |

### B.3 Tools (Pro mode)

| Keys | Tool |
|---|---|
| V | Selection |
| C | Blade (razor) |
| B | Ripple edit |
| R | Rate stretch |
| Y / U | Slip / slide |
| H (hold Space in viewer) | Hand |
| Z | Zoom |
| Q (canvas) | Shape (cycles rectangle, ellipse, polygon, star) |
| G | Pen |
| T | Text |
| E | Rotation |
| X | Annotation (arrow, box, spotlight) |

The Q/W edit keys apply only when the timeline has focus; Q/W as tools
apply only in the canvas. This mirrors how After Effects and Premiere share
letters.

### B.4 Keyframes and animation

| Keys | Action |
|---|---|
| ⌥K | Add / remove keyframe for the selected property at the playhead |
| F9 | Easy ease |
| ⇧F9 / ⌘⇧F9 | Ease in / ease out |
| ⌥⌘H | Toggle hold keyframe |
| ⇧F3 | Graph editor |
| U | Show animated properties of the selected layer |
| P / S / R / T | Show position / scale / rotation / opacity |

### B.5 View and workspace

| Keys | Action |
|---|---|
| = / − | Zoom timeline in / out |
| \ | Fit timeline to window |
| ⌘= / ⌘− | Zoom preview in / out |
| ⇧Z | Fit preview |
| ` (backtick) | Maximize the panel under the pointer |
| ⌘' | Grid |
| ⌘R | Rulers *(conflict with speed in Premiere; Lectern uses ⌘R for speed in the timeline, rulers in the canvas)* |
| ⌘; | Guides |
| ⇧1 … ⇧7 | Panels: Project, Viewer, Timeline, Effect Controls, Effects, Color, Audio |
| ⌥1 / ⌥2 / ⌥3 | Simple / Pro / Color workspace |
| ⌘⇧P | Command palette |
| ⌘, | Settings |

### B.6 Color workspace

| Keys | Action |
|---|---|
| ⌥S | Add corrector after the current one |
| ⇧S | Add corrector before |
| ⌘D (Color workspace only; ⌘D duplicates elsewhere) | Enable / disable current corrector |
| ⇧D | Bypass all grades (before/after) |
| ⌘W | Wipe compare on/off |
| ⌥⌘G | Grab still |
| = | Copy the grade of the previous clip |
| ⇧H | Highlight the qualifier/window matte |
| ↑ / ↓ (color page) | Previous / next clip |

### B.7 Keymap presets

Settings → Keyboard: **Lectern** (default), **Premiere Pro**, **DaVinci
Resolve**, **After Effects**, **Final Cut Pro**; every command can be
rebound; conflicts are shown; export/import as JSON; print as PDF.

---

## C. Reference keymaps

### C.1 Adobe After Effects

| Keys | Action |
|---|---|
| V | Selection tool |
| H | Hand tool |
| Z | Zoom tool |
| W | Rotation tool |
| C | Camera tools (cycle) |
| Y | Pan Behind (anchor point) tool |
| Q | Shape tools (cycle) |
| G | Pen tool (cycle) |
| ⌘T | Type tool (cycle) |
| ⌘B | Brush / Clone Stamp / Eraser (cycle) |
| ⌥W | Roto Brush / Refine Edge |
| ⌘P | Puppet tools (cycle) |
| Space | Preview play / stop |
| ⌘N | New composition |
| ⌘K | Composition settings |
| ⌘Y | New solid |
| ⌥⌘Y | New adjustment layer |
| ⌥⇧⌘Y | New null object |
| ⌥⇧⌘T | New text layer |
| ⌘D | Duplicate |
| ⇧⌘D | Split layer |
| ⇧⌘C | Pre-compose |
| [ / ] | Move layer in / out point to the current time |
| ⌥[ / ⌥] | Trim layer in / out to the current time |
| J / K | Previous / next visible keyframe or marker |
| ⌘→ / ⌘← | Next / previous frame |
| Home / End | Start / end of composition |
| B / N | Set work area start / end |
| P S R T A | Show position / scale / rotation / opacity / anchor point |
| U / UU | Show animated / modified properties |
| E / M / F | Show effects / masks / mask feather |
| F9 | Easy Ease |
| ⇧F9 / ⌘⇧F9 | Easy Ease In / Out |
| ⌥⌘H | Toggle hold keyframe *(verify)* |
| ⇧F3 | Graph Editor |
| ⌥⌘F | Fit layer to composition |
| ⌘= / ⌘− | Zoom in / out (viewer) |
| = / − | Zoom timeline in / out *(main keyboard)* |
| ` | Maximize panel |
| ⌘' | Grid |
| ⌘R | Rulers |
| ⌘; | Guides |
| ⇧⌘H | Show / hide layer controls |
| ⌘L / ⇧⌘L | Lock layer / unlock all |
| ⌃⌘M | Add to Render Queue *(verify)* |
| ⌘I | Import |
| ⌘Z / ⇧⌘Z | Undo / redo |

### C.2 Adobe Premiere Pro

| Keys | Action |
|---|---|
| V | Selection tool |
| A / ⇧A | Track Select Forward / Backward |
| B | Ripple Edit tool |
| N | Rolling Edit tool |
| R | Rate Stretch tool |
| C | Razor tool |
| Y | Slip tool |
| U | Slide tool |
| P | Pen tool |
| H | Hand tool |
| Z | Zoom tool |
| T | Type tool |
| J / K / L | Shuttle left / stop / right |
| Space | Play / stop |
| ← / → | Previous / next frame |
| ↑ / ↓ | Previous / next edit point |
| I / O | Mark in / out |
| ⌥X | Clear in and out |
| X | Mark clip |
| , | Insert |
| . | Overwrite |
| ; | Lift |
| ' | Extract |
| ⌘K | Add edit |
| ⇧⌘K | Add edit to all tracks |
| Q / W | Ripple trim previous / next edit to playhead |
| ⌥⌫ | Ripple delete |
| M | Add marker |
| ⇧M / ⇧⌘M | Next / previous marker |
| ⌘D | Apply default video transition |
| ⇧⌘D | Apply default audio transition |
| ⇧D | Apply default transitions to selection |
| = / − | Zoom timeline in / out |
| \ | Zoom to sequence |
| S | Snap on/off |
| ⌘L | Link / unlink |
| ⌘R | Speed / Duration |
| ⇧E | Enable / disable clip |
| ⌥⌘V | Paste attributes |
| ⇧⌘V | Paste insert |
| ⌘N | New sequence |
| ⌘I | Import |
| ⌘M | Export media |
| ⇧1 … ⇧7 | Project, Source, Timeline, Program, Effect Controls, Audio Track Mixer, Effects *(verify)* |
| ` | Maximize panel |

### C.3 DaVinci Resolve

| Keys | Action |
|---|---|
| ⇧2 … ⇧8 | Media, Cut, Edit, Fusion, Color, Fairlight, Deliver pages *(verify for Resolve 21's extra page)* |
| ⇧9 | Project settings |
| A | Selection (Normal Edit) mode |
| T | Trim Edit mode |
| B | Blade Edit mode |
| ⌘B | Split clip at playhead |
| N | Snapping on/off |
| ⇧⌘L | Linked selection on/off |
| J / K / L | Shuttle reverse / stop / forward |
| Space | Play / stop |
| ← / → | Previous / next frame |
| ↑ / ↓ | Previous / next edit |
| I / O | Mark in / out |
| ⌥X | Clear in and out |
| F9 | Insert |
| F10 | Overwrite |
| F11 | Replace |
| F12 | Place on top |
| ⇧F10 | Ripple overwrite |
| ⇧F11 | Fit to fill |
| ⇧F12 | Append at end |
| ⌫ / ⇧⌫ | Delete / ripple delete *(which one leaves a gap differs by setup — verify)* |
| M | Add marker |
| ⌘T | Add transition |
| ⌘R | Retime controls |
| ⇧Z | Zoom to fit |
| ⌘= / ⌘− | Zoom in / out |
| ⌥S | Color: add serial node |
| ⇧S | Color: add serial node before current |
| ⌥P | Color: add parallel node |
| ⌥L | Color: add layer node |
| ⌥O | Color: add outside node |
| ⌘D | Color: enable / disable current node |
| ⇧D | Color: bypass all grades |
| ⌥⌘G | Color: grab still |
| ⌘W | Color: wipe on/off |
| = | Color: apply grade from previous clip |
| ⇧H | Color: highlight mode |
| ⌥F | Enhanced viewer |
| ⌘F | Cinema (full-screen) viewer |

### C.4 Final Cut Pro

| Keys | Action |
|---|---|
| A | Select tool |
| T | Trim tool |
| P | Position tool |
| R | Range Selection tool |
| B | Blade tool |
| Z | Zoom tool |
| H | Hand tool |
| ⌘B | Blade at playhead |
| ⇧⌘B | Blade all |
| Q | Connect to primary storyline |
| W | Insert |
| D | Overwrite |
| E | Append to storyline |
| J / K / L | Play reverse / stop / forward |
| Space | Play / pause |
| I / O | Set range start / end |
| M | Add marker |
| N | Snapping on/off |
| S | Skimming on/off |
| ⌘T | Add default transition |
| ⌘R | Retime editor |
| ⇧Z | Zoom to fit |
| ⌥G | New compound clip |
| ⌘= / ⌘− | Zoom timeline in / out |
| ⌘4 … ⌘8 | Inspector, Effects, Color board, Audio meters *(verify per version)* |
