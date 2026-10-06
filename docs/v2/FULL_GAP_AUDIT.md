# Full Gap Audit — Lectern vs. DaVinci Resolve, After Effects, Premiere Pro

One list of **everything still missing**, across all areas, in one place.
The other v2 documents go deep on single areas; this document:

1. links to them for the areas they cover (§0),
2. lists every area they do **not** cover (§1–§12): media, editing, audio,
   Fusion/motion graphics, delivery, captions, AI, collaboration, project
   management, performance, platform,
3. lists every **icon** the full feature set needs, which exist and which
   must be drawn (§13),
4. gives one combined priority order (§14),
5. adds the areas found missing in the review pass: recording vs. other
   screen recorders (§16), privacy and redaction (§17), shipping the
   product (§18).

Status (2026-10-05): audit only. ✅/🟡 rows were checked against the
source tree. App columns: ● native · ◐ partly / via another tool · – none.
Feature descriptions of the reference apps are from general product
knowledge (Resolve 19–21, AE/PR 2024–2026); verify details against the
vendors' manuals before building each item.

---

## 0. Areas covered elsewhere

| Area | Document | Items | Missing |
|---|---|---|---|
| Color grading, effects, transitions, keying, compositing, motion, text effects, AI looks | v2/COLOR_EFFECTS_PARITY.md | 230 | 186 |
| Workspace, tools, viewer, panels, timeline UI, graph editor, Character panel, menus, shortcuts summary | v2/PRO_INTERFACE_PARITY.md | 203 | 132 |
| DaVinci Color page, control by control | v2/DAVINCI_COLOR_PAGE.md | 104 | 84 |
| Every keyboard shortcut (Lectern plan + AE/PR/DR reference) | v2/SHORTCUTS.md / SHORTCUTS.pdf | – | – |
| This audit (areas below) | v2/FULL_GAP_AUDIT.md | see §15 | |

---

## 1. Media management (Resolve Media page, PR Project panel, AE Project panel)

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 1.1 | Media browser of disks / folders with preview | ◐ | ● (Media Browser) | ● (Media Storage) | ⬜ | P1 |
| 1.2 | Bins, sub-bins, smart bins (rules) | ● | ● | ● | ⬜ | P2 |
| 1.3 | Metadata view and editing (scene, shot, take, keywords, comments) | ◐ | ● | ● | ⬜ | P2 |
| 1.4 | Clip attributes: fps, pixel aspect, field order, data levels, audio channel mapping | ● | ● | ● | ⬜ | P2 |
| 1.5 | Relink / reconnect offline media | ● | ● | ● | ⬜ | P1 |
| 1.6 | Media management: copy, move, transcode, trim unused, consolidate | ● (Collect Files) | ● (Project Manager) | ● | 🟡 (import copies into project) | P2 |
| 1.7 | Proxy generation and toggle (proxy / original) | ● | ● | ● | ⬜ | P1 |
| 1.8 | Optimized media, render cache | ● | ● | ● | ⬜ | P1 |
| 1.9 | Sync audio and video by waveform or timecode | – | ● | ● | ⬜ | P2 (external mic recordings) |
| 1.10 | Clone tool (verified card offload) | – | – | ● | ⬜ | P3 |
| 1.11 | Image sequences, still images with duration defaults | ● | ● | ● | 🟡 (stills) | P2 |
| 1.12 | Import formats: ProRes, DNxHR, HEVC/H.265, AV1, VP9, MKV, MXF, BRAW, R3D, EXR, PSD layers, AI/SVG | ● | ● | ● | 🟡 (FFmpeg formats; no PSD/SVG/RAW) | P2 |
| 1.13 | Import of other projects: XML, AAF, EDL, FCPXML, OTIO | ◐ | ● | ● | ⬜ | P3 |
| 1.14 | Thumbnail scrubbing (hover scrub) in bins | ◐ | ● | ● | ⬜ | P2 |
| 1.15 | Favorites / ratings / flags / colored labels on clips | ● | ● | ● | ⬜ | P3 |
| 1.16 | Transcription of clips at import (searchable) | – | ● | ● | ⬜ | P1 |

## 2. Editing (Resolve Cut + Edit pages, Premiere timeline)

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 2.1 | Split, trim, ripple delete, move, gap delete | ◐ | ● | ● | ✅ | – |
| 2.2 | Ripple / roll / slip / slide edits (tools and keys) | – | ● | ● | ⬜ | P1 |
| 2.3 | Insert / overwrite / replace / fit-to-fill / place on top / append at end | – | ● | ● | ⬜ | P1 |
| 2.4 | Three-point and four-point editing, source monitor with in/out | – | ● | ● | ⬜ | P2 |
| 2.5 | Track targeting and source patching | – | ● | ● | ⬜ | P2 |
| 2.6 | Linked selection on/off; link/unlink clips; audio/video sync offset warnings | ◐ | ● | ● | 🟡 (Cut Track/All mode) | P1 |
| 2.7 | Snapping, magnetic timeline option | ● | ● | ● | ✅ | – |
| 2.8 | Multiple timelines / sequences in one project; open in tabs | ● | ● | ● | ⬜ | P2 |
| 2.9 | Nested timelines / compound clips | ● | ● | ● | ⬜ | P2 |
| 2.10 | Multicam editing (angles synced, cut live) | – | ● | ● | ⬜ | P2 (screen + camera + phone are natural angles) |
| 2.11 | Markers: colors, notes, durations, chapter markers, marker list / index | ● | ● | ● | 🟡 | P1 |
| 2.12 | Edit index / EDL list of all edits | – | ◐ | ● | ⬜ | P3 |
| 2.13 | Timeline track height per track, track colors, collapse | ◐ | ● | ● | 🟡 | P2 |
| 2.14 | Audio + video track count unlimited; add/delete tracks | ● | ● | ● | ✅ | – |
| 2.15 | Disable clip, solo clip, clip color, rename clip | ● | ● | ● | 🟡 (enable/disable) | P1 |
| 2.16 | Scene cut detection (split an exported video at cuts) | ◐ | ● | ● | ⬜ | P3 |
| 2.17 | Remove silences / pauses | – | ◐ (text-based) | ● (AI) | ✅ | – |
| 2.18 | Text-based editing (cut by deleting words in the transcript) | – | ● | ● (IntelliScript, text-based) | ⬜ | P1 |
| 2.19 | Filler-word removal ("um", "uh") | – | ● | ● | ⬜ | P1 |
| 2.20 | Smart reframe / auto-reframe for vertical | – | ● | ● | ⬜ | P1 |
| 2.21 | Cut page tools: source tape, smart insert, close-up, dual timeline, boring detector | – | – | ● | ⬜ | P3 |
| 2.22 | Speed change, freeze frame, reverse (edit-level) | ● | ● | ● | ⬜ | P1 |
| 2.23 | Copy/paste clips, paste attributes, duplicate | ● | ● | ● | ⬜ | P1 |
| 2.24 | Undo history list; unlimited undo | ● | ● | ● | 🟡 (bounded, no list) | P2 |
| 2.25 | Timeline zoom presets, fit, follow playhead | ● | ● | ● | ✅ | – |
| 2.26 | Playhead-based selection (select clips under playhead, forward from playhead) | – | ● | ● | ⬜ | P2 |
| 2.27 | Gap detection, close all gaps | – | ● | ● | ⬜ | P1 |
| 2.28 | Lock/hide/mute tracks | ● | ● | ● | ✅ | – |
| 2.29 | Layout regions (screen/camera layouts by section) | – | – | – | ✅ | – (Lectern-specific) |
| 2.30 | Connected clips / storylines: overlays, text and music stay attached to a recording segment through ripple edits (Final Cut) | – | ◐ | ◐ | ⬜ | P1 |
| 2.31 | Pro trim mode: dual-roller, asymmetric trim, trim while looping playback (Avid) | – | ● | ● | ⬜ | P2 |
| 2.32 | Volume / opacity envelopes drawn on the clip (Vegas "event envelopes") | ● | ● | ● | ⬜ | P1 |

## 3. Audio (Fairlight, Premiere Essential Sound, AE audio)

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 3.1 | Clip gain, track gain, mute, solo | ● | ● | ● | ✅ | – |
| 3.2 | Fades in/out, crossfades between clips | ◐ | ● | ● | 🟡 (fades; no crossfades) | P1 |
| 3.3 | Volume keyframes / rubber band on the clip | ● | ● | ● | ⬜ | P1 |
| 3.4 | Mixer: faders, pan, meters per track and master bus | – | ● | ● | 🟡 (master meter) | P1 |
| 3.5 | Loudness normalization (LUFS target: −14 YouTube, −16 podcast) and loudness meter | – | ● | ● | ⬜ | P1 |
| 3.6 | Noise reduction / voice isolation (AI) | – | ● (Enhance Speech) | ● (Voice Isolation) | ⬜ | P0 for a recorder |
| 3.7 | Dialogue leveler / auto volume | – | ● | ● | ⬜ | P1 |
| 3.8 | EQ (parametric, presets), high-pass | ● | ● | ● | ⬜ | P1 |
| 3.9 | Compressor, limiter, gate, de-esser, expander | ◐ | ● | ● | ⬜ | P1 |
| 3.10 | Reverb, delay, pitch, chorus, modulation | ● | ● | ● | ⬜ | P3 |
| 3.11 | Auto-ducking music under voice | – | ● | ● | ⬜ | P1 |
| 3.12 | Remix / retime music to length | – | ● | ◐ | ⬜ | P3 |
| 3.13 | Audio track types: mono, stereo, 5.1, 7.1, Atmos | ◐ | ● | ● | 🟡 (mono/stereo) | P3 |
| 3.14 | Buses, sends, submixes | – | ● | ● | ⬜ | P3 |
| 3.15 | Voice-over recording into the timeline (punch-in) | – | ● | ● | ⬜ | P1 (natural for Lectern) |
| 3.16 | ADR, foley sampler, sound library | – | ◐ | ● | ⬜ | P3 |
| 3.17 | Waveform display, zoomable, spectral view | ● | ● | ● | 🟡 (waveform) | P2 |
| 3.18 | VST3 / AU plug-in hosting | – | ● | ● | ⬜ | P3 |
| 3.19 | Audio sync drift correction for long recordings | – | ◐ | ◐ | 🟡 (recording engine) | – |
| 3.20 | Transcribe audio to captions | – | ● | ● | ⬜ | P0 (see §6) |
| 3.21 | De-reverb (room echo removal) | – | ● | ● | ⬜ | P1 |
| 3.22 | De-hum (50/60 Hz), de-click, de-crackle, de-plosive, breath and mouth-click removal (iZotope RX class) | – | ◐ | ● | ⬜ | P2 |
| 3.23 | One-click "Clean up voice" chain (isolation + de-reverb + EQ + compression + loudness), like RX Repair Assistant | – | ◐ | ◐ | ⬜ | P0 |
| 3.24 | Audio roles (dialogue, music, effects) and stem export | – | ◐ | ● | ⬜ | P2 |
| 3.25 | Live monitoring of the mic with the cleanup applied while recording | – | – | ◐ | ⬜ | P2 |

## 4. Fusion / motion graphics page (and AE compositions)

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 4.1 | Node-based compositor (Fusion) | – | – | ● | ⬜ | P3 |
| 4.2 | Layer-based compositions (AE comps) | ● | – | – | 🟡 (tracks as layers) | P2 |
| 4.3 | Generators: background, solid, gradient, noise, shapes | ● | ● | ● | 🟡 (background colors) | P1 |
| 4.4 | Shape layers with fill, stroke, trim paths, repeater, round corners | ● | ◐ | ● | ⬜ | P2 |
| 4.5 | Text+ / Text tool with animators and follow-path | ● | ◐ | ● | 🟡 | P2 |
| 4.6 | Templates: titles, lower thirds, transitions, effects (MOGRT / Fusion macros) | ● | ● | ● | 🟡 (text presets) | P1 |
| 4.7 | Template parameters exposed to the editor (Essential Graphics) | ● | ● | ● | ⬜ | P2 |
| 4.8 | Particles, 3D scenes, cameras, lights | ● | – | ● | ⬜ | P3 |
| 4.9 | Planar tracker, camera tracker | ● | ◐ | ● | ⬜ | P2 |
| 4.10 | Rotoscoping (bezier, B-spline), paint | ● | – | ● | ⬜ | P3 |
| 4.11 | Expressions / scripting of parameters | ● | – | ● | ⬜ | P3 |
| 4.12 | Lottie / SVG animation import | ◐ | ◐ | – | ⬜ | P2 (stickers, animated icons) |
| 4.13 | Animated callouts: arrows, circles, highlight boxes, spotlight, blur box | ◐ | ◐ | ◐ | ⬜ | P1 (Lectern-specific tutorial value) |
| 4.14 | Cursor effects: smoothing, size, highlight, click ripple, hide when idle | – | – | – | ⬜ | P0 (screen-recorder must-have) |
| 4.15 | Auto zoom on clicks / typing (Screen Studio style) | – | – | – | ⬜ | P0 |
| 4.16 | Device frames (laptop, phone mockups) around screen recordings | – | – | – | ⬜ | P2 |
| 4.17 | Stock media, music, sound effects, stickers/emoji and GIF library | ◐ | ● | ◐ | ⬜ | P2 |
| 4.18 | Ready-made YouTube elements: intro/outro, subscribe button, progress bar, countdown, end screen | ◐ | ◐ | ◐ | ⬜ | P2 |

## 5. Delivery (Deliver page, Media Encoder, AE Render Queue)

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 5.1 | MP4 H.264 + AAC export | ● | ● | ● | ✅ | – |
| 5.2 | H.265/HEVC, AV1, ProRes (422, 4444), DNxHR, GIF, WebM/VP9 | ● | ● | ● | ⬜ | P1 (HEVC, ProRes, GIF, WebM first) |
| 5.3 | Hardware encoding (VideoToolbox, NVENC, QuickSync, AMF) | ● | ● | ● | 🟡 (check per platform) | P1 |
| 5.4 | Presets per platform (YouTube, Shorts, TikTok, Instagram, LinkedIn, X) | ◐ | ● | ● | 🟡 (aspect presets) | P1 |
| 5.5 | Resolution, frame rate, bitrate modes (CBR/VBR/CRF), quality slider | ● | ● | ● | 🟡 | P1 |
| 5.6 | Export range: whole, in/out, selected clips, markers as chapters | ● | ● | ● | ⬜ | P1 |
| 5.7 | Render queue with several jobs, background rendering | ● | ● (Media Encoder) | ● | 🟡 (one background job) | P2 |
| 5.8 | Audio-only export (WAV, MP3, AAC) | ● | ● | ● | ⬜ | P1 |
| 5.9 | Image export: current frame as PNG/JPEG, image sequence | ● | ● | ● | 🟡 (CLI --frame) | P1 |
| 5.10 | Subtitle export burned in or as sidecar (SRT/VTT) or embedded track | ◐ | ● | ● | 🟡 (SRT/VTT sidecar) | P1 |
| 5.11 | Chapters embedded in MP4 / YouTube chapter text from markers | – | ◐ | ◐ | ⬜ | P1 |
| 5.12 | Direct upload: YouTube, Vimeo, TikTok, X, Frame.io, Dropbox | ◐ | ● | ● | ⬜ | P2 |
| 5.13 | Shareable link with viewer comments (Loom/Tella style) | – | ◐ (Frame.io) | ◐ | ⬜ | P1 (Lectern product fit) |
| 5.14 | HDR export metadata (HDR10, HLG, Dolby Vision) | ● | ● | ● | ⬜ | P3 |
| 5.15 | Alpha export (ProRes 4444, PNG sequence) | ● | ● | ● | ⬜ | P2 |
| 5.16 | Smart render / render cache reuse on export | ● | ● | ● | ⬜ | P2 |
| 5.17 | Export several aspect ratios in one go | – | ◐ | ◐ | ⬜ | P1 |
| 5.18 | Thumbnail / poster frame generator | – | ◐ | – | ⬜ | P2 |
| 5.19 | Interactive share page: call-to-action button, links, chapters, viewer analytics, password, expiry | – | – | – | ⬜ | P1 (Loom/Tella-style) |
| 5.20 | Embed code and auto-generated transcript page for sharing | – | – | – | ⬜ | P2 |
| 5.21 | Export presets saved by the user | ● | ● | ● | ⬜ | P1 |

## 6. Captions and subtitles

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 6.1 | Subtitle track, add/edit lines | ◐ | ● | ● | ✅ | – |
| 6.2 | SRT / VTT import and export | ◐ | ● | ● | ✅ | – |
| 6.3 | Auto transcription (speech to text), many languages | – | ● | ● | ⬜ | P0 |
| 6.4 | Speaker detection / labels | – | ● | ● | ⬜ | P2 |
| 6.5 | Caption styles: font, size, color, background, position | ◐ | ● | ● | ✅ | – |
| 6.6 | Animated captions (word-by-word highlight, karaoke, pop) | ◐ | ● | ● | ⬜ | P1 |
| 6.7 | Caption line length / duration rules, split/merge lines | – | ● | ● | ⬜ | P2 |
| 6.8 | Translation of captions | – | ◐ | ● | ⬜ | P2 |
| 6.9 | Burn-in vs. sidecar choice on export | ◐ | ● | ● | 🟡 | P1 |
| 6.10 | CEA-608/708, TTML, SCC broadcast formats | – | ● | ● | ⬜ | P3 |

## 7. AI features (all areas, not color — color AI is in COLOR_EFFECTS_PARITY §18)

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 7.1 | Transcription + text-based edit | – | ● | ● | ⬜ | P0 |
| 7.2 | Voice isolation / speech enhance | – | ● | ● | ⬜ | P0 |
| 7.3 | Silence and filler-word removal | – | ● | ● | 🟡 (silence) | P1 |
| 7.4 | Auto reframe | – | ● | ● | ⬜ | P1 |
| 7.5 | Person segmentation (background blur/replace) | ◐ | ◐ | ● | 🟡 | P1 |
| 7.6 | Eye contact correction for webcam | – | – | ◐ | ⬜ | P2 (talking heads) |
| 7.7 | Generative extend / AI fill frames | – | ● | ◐ | ⬜ | P3 |
| 7.8 | Scene detection, smart bins by content (faces, objects) | – | ◐ | ● | ⬜ | P3 |
| 7.9 | AI music remix / beat detection, cut to beat | – | ● | ● | ⬜ | P3 |
| 7.10 | Auto chapters and titles from the transcript | – | – | – | ⬜ | P1 (Lectern-specific) |
| 7.11 | AI voice-over / dubbing / translation of speech | – | ◐ | ● | ⬜ | P3 |
| 7.12 | Highlight clips for shorts from a long recording | – | ◐ | ◐ | ⬜ | P2 |

## 8. Collaboration and review

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 8.1 | Comments / review on frames (Frame.io, Blackmagic Cloud) | ● | ● | ● | ⬜ | P1 (share-link comments) |
| 8.2 | Multi-user project, bin locking | ◐ | ● (Team Projects) | ● | ⬜ | P3 |
| 8.3 | Cloud project sync | ◐ | ● | ● | ⬜ | P3 |
| 8.4 | Version history of the project | ◐ | ● | ● | 🟡 (undo + autosave) | P2 |
| 8.5 | Remote grading / remote monitoring | – | – | ● | ⬜ | P3 |
| 8.6 | Notes / to-do markers assigned to people | – | ◐ | ● | ⬜ | P3 |

## 9. Project, settings and preferences

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 9.1 | Project manager (list, search, thumbnails, open recent) | ◐ | ● | ● | ✅ (home) | – |
| 9.2 | Autosave, backups, crash recovery | ● | ● | ● | ✅ | – |
| 9.3 | Project settings: resolution, fps, color science, audio sample rate | ● | ● | ● | 🟡 | P1 |
| 9.4 | Preferences: memory/GPU, cache location, auto-save interval, UI scale, language | ● | ● | ● | ⬜ | P1 |
| 9.5 | Keyboard customization and presets | ● | ● | ● | ⬜ | P2 |
| 9.6 | Archive / export project with media (.drp/.dra, Productions) | ● | ● | ● | ⬜ | P2 |
| 9.7 | Project templates | ◐ | ● | ● | ⬜ | P2 |
| 9.8 | Localization (UI languages) | ● | ● | ● | ⬜ | P2 |
| 9.9 | Scripting API (ExtendScript/UXP, Resolve Python/Lua) | ● | ● | ● | ⬜ | P3 |
| 9.10 | Plug-in SDK (effects, exporters) | ● | ● | ● | ⬜ | P3 |

## 10. Performance and hardware

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 10.1 | GPU-accelerated decode, effects, encode | ● | ● | ● | 🟡 (HW decode depends on codec) | P0 |
| 10.2 | Playback resolution (full/half/quarter) | ● | ● | ● | ⬜ | P1 |
| 10.3 | Background caching / render in-out | ● | ● | ● | ⬜ | P1 |
| 10.4 | Dropped-frame indicator | – | ● | ● | ⬜ | P2 |
| 10.5 | Multi-GPU | – | – | ● | ⬜ | P3 |
| 10.6 | Apple Silicon / Windows ARM native | ● | ● | ● | 🟡 (Apple Silicon yes; Windows not run yet) | P1 |
| 10.7 | Video I/O cards (DeckLink), control panels | ◐ | ◐ | ● | ⬜ | P3 |

## 11. Platform

| # | Feature | AE | PR | DR | Lectern | P |
|---|---|---|---|---|---|---|
| 11.1 | macOS | ● | ● | ● | ✅ | – |
| 11.2 | Windows | ● | ● | ● | 🟡 (compile-checked, never run) | P0 |
| 11.3 | Linux | – | – | ● | ⬜ | P3 |
| 11.4 | iPad / mobile editor | – | ◐ (Premiere mobile) | ● (iPad) | ⬜ | P3 |
| 11.5 | Phone as camera (live) | – | – | ◐ | 🟡 (built, not tested on a phone) | P1 |

## 12. Lectern recorder advantages to keep (not in the reference apps)

| # | Feature | Lectern |
|---|---|---|
| 12.1 | Screen + camera + mic + system audio recording with separate tracks | ✅ |
| 12.2 | Crash-safe recording with recovery | ✅ |
| 12.3 | Layout presets (camera corner, side by side…) with sections | ✅ |
| 12.4 | Phone as a camera over Wi-Fi | 🟡 |
| 12.5 | Canvas editing of screen/camera placement | ✅ |

---

## 13. Icon inventory (all icons the full feature set needs)

Lectern has **59** SVG icons in `src/ui/icons/`:
adjust, animation, app, aspect, audio, camera, check, chevron-down,
chevron-left, chevron-right, close, copy, cut, download, effects, export,
eye-off, eye, fit, folder, home, image, layout, lock, magnet, marker, mic,
music, overlay, pause, phone, play, plus, pointer, record, redo, screen,
select-box, setup, sparkle, speaker-off, speaker, step-back, step-forward,
stop, style, subtitles, text, timer, trash, undo, unlock, upload, warning,
wifi, window.

Style rules for new icons: same as the existing set — 24 px grid, 1.75 px
stroke, round caps, monochrome (`currentColor`), no fill except small dots;
names in kebab-case; each listed in IconProvider.

Icons to add, grouped by where they are used (✚ = new; ≈ = an existing icon
can be reused):

### 13.1 Tools bar (PRO_INTERFACE §2)
✚ hand · ✚ zoom-in · ✚ zoom-out · ✚ rotate · ✚ anchor-point · ✚ orbit ·
✚ pan-camera · ✚ dolly · ✚ rectangle · ✚ rounded-rectangle · ✚ ellipse ·
✚ polygon · ✚ star · ✚ pen · ✚ pen-add · ✚ pen-remove · ✚ pen-convert ·
✚ mask-feather · ✚ type-horizontal · ✚ type-vertical · ✚ brush ·
✚ clone-stamp · ✚ eraser · ✚ roto-brush · ✚ refine-edge · ✚ puppet-pin ·
✚ puppet-starch · ✚ puppet-bend · ✚ blade · ✚ ripple-edit · ✚ roll-edit ·
✚ slip · ✚ slide · ✚ rate-stretch · ✚ track-select · ✚ arrow-callout ·
✚ highlight-box · ✚ spotlight · ≈ pointer (selection) · ≈ magnet (snapping) ·
≈ home · ≈ text

### 13.2 Timeline switches and layer controls (PRO_INTERFACE §6)
✚ solo · ✚ shy · ✚ collapse-transform · ✚ quality-draft · ✚ quality-best ·
✚ fx · ✚ frame-blend · ✚ motion-blur · ✚ adjustment-layer · ✚ layer-3d ·
✚ parent-link (pick whip) · ✚ track-matte · ✚ blend-mode · ✚ label-color ·
✚ stopwatch · ✚ keyframe · ✚ keyframe-hold · ✚ keyframe-ease ·
✚ keyframe-prev · ✚ keyframe-next · ✚ graph-editor · ✚ easy-ease ·
✚ work-area · ✚ precompose · ✚ duplicate · ✚ link · ✚ unlink ·
✚ chevron-up · ≈ eye / eye-off · ≈ speaker / speaker-off · ≈ lock / unlock ·
≈ marker · ≈ cut (split)

### 13.3 Viewer (PRO_INTERFACE §3, DAVINCI_COLOR_PAGE §2–3)
✚ grid · ✚ guides · ✚ rulers · ✚ safe-margins · ✚ transparency-grid ·
✚ channel-rgb · ✚ channel-alpha · ✚ snapshot · ✚ show-snapshot ·
✚ split-compare · ✚ wipe · ✚ region-of-interest · ✚ resolution ·
✚ full-screen · ✚ loop · ✚ go-to-start · ✚ go-to-end · ✚ play-reverse ·
✚ highlight-matte · ✚ eyedropper · ✚ eyedropper-plus · ✚ eyedropper-minus ·
≈ fit · ≈ play / pause / stop · ≈ step-back / step-forward

### 13.4 Color page (DAVINCI_COLOR_PAGE §4–9)
✚ camera-raw · ✚ color-match · ✚ color-wheels · ✚ hdr-wheels ·
✚ primaries-bars · ✚ log-wheels · ✚ rgb-mixer · ✚ motion-effects ·
✚ curves · ✚ color-slice · ✚ color-warper · ✚ qualifier · ✚ window-shape ·
✚ window-linear · ✚ window-circle · ✚ window-polygon · ✚ window-curve ·
✚ window-gradient · ✚ tracker · ✚ magic-mask · ✚ blur-palette · ✚ key ·
✚ sizing · ✚ stereo-3d · ✚ keyframes-palette · ✚ scopes · ✚ info ·
✚ auto-balance · ✚ white-balance-picker · ✚ black-point · ✚ white-point ·
✚ reset · ✚ node-serial · ✚ node-parallel · ✚ node-layer-mixer ·
✚ node-outside · ✚ gallery · ✚ grab-still · ✚ powergrade · ✚ waveform ·
✚ parade · ✚ vectorscope · ✚ histogram · ≈ adjust (Color tool rail) ·
≈ sparkle (AI)

### 13.5 Panels and pages
✚ project-panel · ✚ bin · ✚ smart-bin · ✚ effect-controls · ✚ effects-presets ·
✚ character · ✚ paragraph · ✚ align-left · ✚ align-center · ✚ align-right ·
✚ align-justify · ✚ distribute · ✚ align-panel · ✚ tracker-panel ·
✚ essential-graphics · ✚ history · ✚ render-queue · ✚ command-palette ·
✚ workspace · ✚ panel-maximize · ✚ panel-float · ✚ search · ✚ list-view ·
✚ grid-view · ✚ sort · ✚ filter · ✚ more (···) · ✚ settings (gear) ·
✚ page-media · ✚ page-cut · ✚ page-edit · ✚ page-fusion · ✚ page-color ·
✚ page-fairlight · ✚ page-deliver · ≈ folder · ≈ upload / download · ≈ home

### 13.6 Text (PRO_INTERFACE §8)
✚ font · ✚ font-size · ✚ leading · ✚ kerning · ✚ tracking · ✚ baseline-shift ·
✚ stroke · ✚ fill · ✚ swap-colors · ✚ no-color · ✚ bold · ✚ italic ·
✚ all-caps · ✚ small-caps · ✚ superscript · ✚ subscript · ✚ text-box ·
✚ text-shadow

### 13.7 Audio
✚ fader · ✚ pan · ✚ eq · ✚ compressor · ✚ noise-reduction · ✚ voice-isolation ·
✚ loudness · ✚ ducking · ✚ crossfade · ✚ volume-keyframe · ✚ record-voiceover ·
✚ waveform-audio · ≈ mic · ≈ music · ≈ speaker

### 13.8 Effects categories (Effects & Presets browser)
✚ cat-blur · ✚ cat-sharpen · ✚ cat-color · ✚ cat-distort · ✚ cat-generate ·
✚ cat-keying · ✚ cat-matte · ✚ cat-noise-grain · ✚ cat-perspective ·
✚ cat-simulation · ✚ cat-stylize · ✚ cat-time · ✚ cat-transition ·
✚ cat-light · ✚ cat-repair · ✚ cat-ai · ✚ favorite (star outline) ·
≈ effects · ≈ animation (presets)

### 13.9 Delivery, captions, collaboration
✚ export-preset · ✚ youtube · ✚ tiktok · ✚ instagram · ✚ share-link ·
✚ comment · ✚ chapters · ✚ transcript · ✚ translate · ✚ speaker-label ·
✚ caption-style · ✚ burn-in · ✚ cursor · ✚ cursor-click · ✚ auto-zoom ·
✚ device-frame · ≈ subtitles · ≈ export

### 13.10 Recording, privacy, assets (§16–§18, §4.17–4.18)
✚ pause-record · ✚ retake · ✚ keystroke · ✚ teleprompter · ✚ draw-on-screen ·
✚ region-capture · ✚ window-capture · ✚ exclude-window · ✚ app-audio ·
✚ redact · ✚ notifications-off · ✚ link-lock · ✚ link-expiry · ✚ analytics ·
✚ call-to-action · ✚ stock-library · ✚ sticker · ✚ progress-bar ·
✚ voice-cleanup · ✚ de-reverb · ≈ record · ≈ screen · ≈ window · ≈ camera ·
≈ phone · ≈ timer

Brand icons (YouTube, TikTok, Instagram) must follow each brand's
guidelines or be replaced with generic labels.

**Icon count:** 59 existing · 249 new listed above (each ✚ once). Draw them in
batches by phase (§14) rather than all at once.

---

## 14. One combined priority order

Across all v2 documents. Items are referenced as DOC §number.

**P0 — foundation and recorder must-haves**
1. GPU float color-managed pipeline (COLOR §1)
2. Auto transcription → captions, text-based editing (AUDIT §6.3, 2.18)
3. Voice isolation / noise reduction (AUDIT §3.6)
4. Cursor effects and auto zoom on clicks (AUDIT §4.14–4.15)
5. Scrubby number fields, Effect Controls with keyframes, effects browser (INTERFACE §5, §9.1)
6. Basic transitions (COLOR §14.1–14.2, 14.8–14.9)
7. Windows: run and fix on real hardware (AUDIT §11.2)
8. Cursor and clicks/keys recorded as data; one-click "Clean up voice" (AUDIT §16.4–16.5, §3.23)
9. Ship basics: signing/notarization, installer, auto-update, codec licensing, consent for telemetry (AUDIT §18.1–18.4, §17.7)

**P1 — what users of a pro editor expect**
Lumetri-level primaries + curves + HSL + scopes (COLOR §2–4, §6; DAVINCI
§5–6); adjustment layers, blend modes, masks, tracking, chroma key (COLOR
§12–13); speed, freeze, copy/paste attributes; loudness, EQ, compressor,
ducking, voice-over (AUDIT §3); export formats, platform presets, chapters,
share links (AUDIT §5); animated captions; Pro workspace with menu bar and
command palette (INTERFACE §1); proxies and render cache.

**P2 — power users**
Node graph and gallery (DAVINCI §8–9), graph editor (INTERFACE §7),
multicam, nesting, bins and metadata, render queue, keymap presets,
templates, HDR wheels, film looks.

**P3 — specialist / maybe never**
Fusion-style node compositing, 3D, particles, expressions, OpenFX/VST
hosting, broadcast caption formats, HDR mastering, control surfaces,
multi-user projects, Linux, iPad.

---

## 15. Count summary (§1–§11 and §16–§18; §12 lists strengths, not counted)

| Section | Items | ✅ | 🟡 | ⬜ |
|---|---|---|---|---|
| 1 Media | 16 | 0 | 3 | 13 |
| 2 Editing | 32 | 7 | 5 | 20 |
| 3 Audio | 25 | 1 | 5 | 19 |
| 4 Motion graphics | 18 | 0 | 4 | 14 |
| 5 Delivery | 21 | 1 | 6 | 14 |
| 6 Captions | 10 | 3 | 1 | 6 |
| 7 AI | 12 | 0 | 2 | 10 |
| 8 Collaboration | 6 | 0 | 1 | 5 |
| 9 Project | 10 | 2 | 1 | 7 |
| 10 Performance | 7 | 0 | 2 | 5 |
| 11 Platform | 5 | 1 | 2 | 2 |
| 16 Recording | 20 | 3 | 5 | 12 |
| 17 Privacy & redaction | 7 | 0 | 2 | 5 |
| 18 Shipping | 10 | 0 | 3 | 7 |
| **Total** | **199** | **18** | **42** | **139** |

Grand total across the four v2 checklists: 230 + 203 + 104 + 199 = **736
items**, of which Lectern has 70 fully and 127 partly.

---

## 16. Recording (Lectern's core) — vs. Camtasia, Screen Studio, Loom, Tella, OBS

The 14 reference tools do not record screens, so recording is checked
against the screen recorders Lectern competes with.

| # | Feature | Camtasia | Screen Studio | Loom | OBS | Lectern | P |
|---|---|---|---|---|---|---|---|
| 16.1 | Full screen, window, region capture | ● | ● | ● | ● | 🟡 (screen; check window/region) | P0 |
| 16.2 | Separate tracks for screen, camera, mic, system audio | ● | ● | ◐ | ● | ✅ | – |
| 16.3 | 4K / 60 fps capture, high-DPI (Retina) | ● | ● | ◐ | ● | 🟡 | P1 |
| 16.4 | Cursor recorded as data (can be restyled, hidden, smoothed after) | ● | ● | – | – | ⬜ | P0 |
| 16.5 | Clicks and keystrokes recorded as events (for auto zoom and key overlays) | ● | ● | – | ◐ | ⬜ | P0 |
| 16.6 | Pause / resume during recording | ● | ● | ● | ● | ⬜ | P1 |
| 16.7 | Countdown, stop hotkey, recording timer | ● | ● | ● | ◐ | ✅ | – |
| 16.8 | Retake last segment / multiple takes | ◐ | – | ◐ | – | ⬜ | P1 (Tella-style clips) |
| 16.9 | Live camera background blur / removal while recording | ◐ | – | ● | ◐ | ⬜ | P2 |
| 16.10 | Live mic noise suppression | ◐ | – | ● | ● | ⬜ | P1 |
| 16.11 | Teleprompter / speaker notes overlay (hidden from capture) | – | – | – | ◐ | ⬜ | P2 |
| 16.12 | Draw / annotate on screen while recording | ● | – | ● | ◐ | ⬜ | P2 |
| 16.13 | Per-app audio capture (only one app's sound) | ◐ | – | – | ● | ⬜ | P2 |
| 16.14 | iPhone/iPad screen capture over USB | ● | ● | – | ◐ | ⬜ | P3 |
| 16.15 | Phone as camera | – | – | – | ◐ | 🟡 | P1 |
| 16.16 | Exclude windows (hide Lectern, notifications) from capture | ◐ | ● | ◐ | ● | 🟡 (check) | P1 |
| 16.17 | Crash-safe recording, recovery | ◐ | ◐ | ● | ● | ✅ | – |
| 16.18 | Disk space / dropped-frame warnings while recording | ◐ | ◐ | ◐ | ● | 🟡 | P1 |
| 16.19 | Instant share after recording (upload while recording) | – | – | ● | – | ⬜ | P1 |
| 16.20 | Scenes and live switching / streaming | – | – | – | ● | ⬜ | P3 |

## 17. Privacy, security and redaction

| # | Feature | Lectern | P |
|---|---|---|---|
| 17.1 | Auto-detect and blur sensitive text in screen recordings (emails, API keys, passwords, credit cards) | ⬜ | P1 |
| 17.2 | Tracked manual redaction box (follows a window or element) | ⬜ | P1 (COLOR §9.11) |
| 17.3 | Hide notifications / Do Not Disturb while recording | ⬜ | P1 |
| 17.4 | Local-only processing for AI features (transcription, segmentation) by default | 🟡 (segmentation is local) | P0 |
| 17.5 | Share links: password, expiry, revoke, download on/off | ⬜ | P1 |
| 17.6 | Permissions explained (screen, camera, mic) with recovery steps | 🟡 | P1 |
| 17.7 | No telemetry without consent; crash reports opt-in | ⬜ | P0 |

## 18. Shipping the product

| # | Item | Lectern | P |
|---|---|---|---|
| 18.1 | macOS signing, notarization, hardened runtime | ⬜ | P0 |
| 18.2 | Windows installer, code signing, run on real hardware | ⬜ | P0 |
| 18.3 | Auto-update (Sparkle / WinSparkle or equivalent) | ⬜ | P0 |
| 18.4 | Codec licensing review (H.264/HEVC/AAC encoders, FFmpeg LGPL build) | ⬜ | P0 |
| 18.5 | Crash reporting (opt-in) | ⬜ | P1 |
| 18.6 | Onboarding: first recording in under a minute, sample project | 🟡 | P1 |
| 18.7 | In-app help, shortcut sheet (v2/SHORTCUTS.pdf), tooltips | 🟡 | P1 |
| 18.8 | Project format versioning and migration tests (v2 → v3 for color layers) | 🟡 | P0 |
| 18.9 | Accessibility (VoiceOver/Narrator, keyboard-only use, contrast) | ⬜ | P2 |
| 18.10 | Localization pipeline | ⬜ | P2 |
