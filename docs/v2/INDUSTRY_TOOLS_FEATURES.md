# Industry Tools — Main Features and What Lectern Takes From Each

The 14 professional tools the user listed (2026-10-05), with their main
features, and for each feature whether Lectern should adopt it. This sets the
ambition level for Lectern's editor; the detailed checklists are in the other
v2 documents:

- v2/COLOR_EFFECTS_PARITY.md — color, effects, transitions, compositing
- v2/PRO_INTERFACE_PARITY.md — workspace, tools, panels, timeline UI
- v2/DAVINCI_COLOR_PAGE.md — Resolve Color page in detail
- v2/FULL_GAP_AUDIT.md — media, editing, audio, delivery, AI, icons
- v2/SHORTCUTS.md (+ .pdf) — keyboard shortcuts

Feature lists are from general product knowledge (2024–2026 versions) and
describe each product's headline capabilities, not every feature. Verify
against vendor documentation before building a specific item.

**Adopt** column: **Yes** = in Lectern's roadmap (with the document §) ·
**Later** = after the core is done · **No** = out of scope for a
recorder/editor (with the reason).

| Tool | Known for | Rating (user) |
|---|---|---|
| DaVinci Resolve | Editing, color grading, VFX, audio — all in one | ⭐⭐⭐⭐⭐ |
| Adobe Premiere Pro | Professional editing: films, YouTube, commercials | ⭐⭐⭐⭐⭐ |
| Final Cut Pro | Fast editing on Mac | ⭐⭐⭐⭐⭐ |
| Avid Media Composer | Hollywood / TV editing | ⭐⭐⭐⭐⭐ |
| Adobe After Effects | Motion graphics, VFX, titles | ⭐⭐⭐⭐⭐ |
| Blackmagic Fusion | Node-based VFX and compositing | ⭐⭐⭐⭐⭐ |
| Nuke | Hollywood VFX compositing | ⭐⭐⭐⭐⭐ |
| Houdini | Procedural VFX, simulations, 3D | ⭐⭐⭐⭐⭐ |
| Cinema 4D | 3D motion graphics | ⭐⭐⭐⭐ |
| Blender | Free 3D, animation, VFX, video editing | ⭐⭐⭐⭐ |
| Vegas Pro | Professional editing on Windows | ⭐⭐⭐⭐ |
| Avid Pro Tools | Film/TV sound post-production | ⭐⭐⭐⭐⭐ |
| Adobe Audition | Audio editing and cleanup | ⭐⭐⭐⭐ |
| iZotope RX | Dialogue and audio repair | ⭐⭐⭐⭐⭐ |

---

## 1. DaVinci Resolve

| Feature | Adopt | Where |
|---|---|---|
| Seven pages (Media, Cut, Edit, Fusion, Color, Fairlight, Deliver) in one app | Yes — as Simple / Pro / Color / Audio modes | INTERFACE §0, DAVINCI §1 |
| Node-based color grading, primaries, curves, qualifiers, power windows, tracking | Yes | DAVINCI §4–8, COLOR §2–5 |
| Color management (DaVinci Wide Gamut, ACES), HDR grading | Yes (HDR later) | COLOR §1 |
| Magic Mask (AI object/person isolation) | Yes | COLOR §4.9 |
| Cut page: fast editing, source tape, dual timeline | Later | AUDIT §2.21 |
| Fusion page: node compositing inside the editor | Later (motion-graphics subset) | AUDIT §4 |
| Fairlight: full DAW, voice isolation, dialogue leveler, AI audio | Yes (cleanup first) | AUDIT §3 |
| Text-based editing / IntelliScript, transcription, subtitles | Yes | AUDIT §2.18, §6 |
| Smart Reframe, Speed Warp, Super Scale (AI) | Yes / Later | COLOR §18 |
| Blackmagic Cloud collaboration, multi-user | Later | AUDIT §8 |
| Deliver page: render queue, presets, direct upload | Yes | AUDIT §5 |
| Free version with most features | Product decision | – |

## 2. Adobe Premiere Pro

| Feature | Adopt | Where |
|---|---|---|
| Timeline editing with ripple/roll/slip/slide, multicam, nested sequences | Yes | AUDIT §2 |
| Lumetri Color panel (Basic, Creative, Curves, Wheels, HSL, Vignette) + scopes | Yes — the first color target | COLOR §2–6 |
| Essential Graphics / Motion Graphics Templates (MOGRT) | Yes | AUDIT §4.6–4.7 |
| Essential Sound (dialogue, music, SFX, ambience; auto-ducking; Enhance Speech) | Yes | AUDIT §3 |
| Text-based editing, speech to text, captions with styles | Yes (P0) | AUDIT §6 |
| Auto Reframe, Scene Edit Detection, Generative Extend | Yes / Later | AUDIT §2.16, 2.20, 7.7 |
| Morph Cut (hide jump cuts) | Yes | COLOR §14.5 |
| Dynamic Link with After Effects, Media Encoder render queue | Later (internal equivalent) | AUDIT §5.7 |
| Productions and Team Projects, Frame.io review | Later | AUDIT §8 |
| Properties panel (contextual controls) | Yes | INTERFACE §5.9 |

## 3. Final Cut Pro

| Feature | Adopt | Where |
|---|---|---|
| Magnetic timeline (clips close gaps, connected clips follow) | Yes — as an option ("magnetic" mode) | AUDIT §2.7 |
| Connected clips and storylines (B-roll attached to the main story) | Yes — attach overlays/text to a recording segment so ripple edits keep them in sync | AUDIT §2.30 |
| Roles (dialogue, music, effects) for audio/video organization and export stems | Yes | AUDIT §3.24 |
| Libraries / events / keyword collections, smart collections | Later | AUDIT §1.2–1.3 |
| Very fast performance on Apple Silicon, background rendering | Yes (performance target) | AUDIT §10 |
| Multicam with automatic sync | Later | AUDIT §2.10 |
| Object tracker, smart conform, cinematic mode editing | Yes / Later | COLOR §16.8, AUDIT §2.20 |
| Transcribe to captions, enhanced light & color | Yes | AUDIT §6.3 |
| Motion app templates (titles, generators) | Yes (templates) | AUDIT §4.6 |
| Skimming (hover preview of clips) | Yes | AUDIT §1.14 |

## 4. Avid Media Composer

| Feature | Adopt | Where |
|---|---|---|
| Bin-based media management, script integration (ScriptSync) | Later (transcript is the modern version) | AUDIT §1, §2.18 |
| Trim mode (dual-roller, asymmetrical trims, trim while playing) | Yes (pro trim mode) | AUDIT §2.31 |
| Source/record editing, three-point editing | Later | AUDIT §2.4 |
| Shared projects and bin locking (Nexis) | No — enterprise storage | – |
| PhraseFind / ScriptSync (search by spoken words) | Yes (via transcription) | AUDIT §6.3 |
| Frame-accurate, rock-solid project format with long-term compatibility | Yes (project format discipline) | ../PROJECT_FORMAT.md |
| Broadcast deliverables (MXF, DNxHD/HR, AAF) | Later (DNxHR export) | AUDIT §5.2 |

## 5. Adobe After Effects

| Feature | Adopt | Where |
|---|---|---|
| Layer-based compositions, precompose, parenting | Yes (subset) | INTERFACE §6, AUDIT §4.2 |
| Keyframes, graph editor, easing | Yes | INTERFACE §6.3, §7 |
| Shape layers, masks, track mattes, blend modes | Yes | COLOR §13, AUDIT §4.4 |
| Text animators | Later | COLOR §17.3 |
| 300+ effects (blur, distort, generate, keying, stylize…) | Yes (the common 50 first) | COLOR §9–12 |
| Roto Brush, Content-Aware Fill | Yes / Later | COLOR §4.9, §8.7 |
| Motion tracking, 3D camera tracker, Mocha | Yes (2D first) | COLOR §16.8 |
| Expressions (JavaScript) | Later | INTERFACE §6.39 |
| 3D layers, cameras, lights, Cinema 4D renderer, native 3D models | Later | COLOR §13.5 |
| Animation presets, MOGRT export | Yes | COLOR §16.10 |

## 6. Blackmagic Fusion

| Feature | Adopt | Where |
|---|---|---|
| Node-based compositing (merge, transform, mask nodes) | Later | AUDIT §4.1 |
| True 3D compositing space, particles | No / Later | – |
| Planar tracker, camera tracker | Yes (planar) | AUDIT §4.9 |
| Rotoscoping and paint | Later | AUDIT §4.10 |
| Text+ with layout and follow-path | Later | AUDIT §4.5 |
| Macros / templates used by the editor | Yes (templates) | AUDIT §4.6 |

## 7. Nuke (Foundry)

| Feature | Adopt | Where |
|---|---|---|
| Industry node compositor, deep compositing, multichannel EXR | No — feature-film VFX | – |
| CopyCat (train AI on your shots), smart vector tools | No | – |
| Pipeline / Python scripting, OCIO color management | OCIO yes (color management) | COLOR §1.4 |
| Lens distortion, 3D camera solve | Later | COLOR §8.9 |

## 8. Houdini (SideFX)

| Feature | Adopt | Where |
|---|---|---|
| Procedural node-based 3D, simulations (fluids, pyro, destruction, crowds) | No — 3D/simulation software, out of scope | – |
| Solaris / Karma rendering, USD | No | – |
| Lesson for Lectern: procedural, non-destructive edits | Yes (already: every edit is a pure, undoable operation) | ../TIMELINE_ENGINE.md |

## 9. Cinema 4D (Maxon)

| Feature | Adopt | Where |
|---|---|---|
| 3D modeling and motion graphics (MoGraph cloners, effectors) | No | – |
| Redshift rendering | No | – |
| 3D text and logo animations for intros | Later — as ready-made templates, not a 3D editor | COLOR §17.9 |

## 10. Blender

| Feature | Adopt | Where |
|---|---|---|
| Full 3D suite (modeling, sculpting, rigging, Cycles/Eevee rendering) | No | – |
| Grease Pencil 2D animation | Later — as screen annotation drawing | INTERFACE §2.11 |
| Compositor nodes, motion tracking | Later | AUDIT §4.9 |
| Video Sequence Editor (basic editing) | Lectern already exceeds it for its use case | – |
| Open source, Python add-ons | Product decision (scripting API later) | AUDIT §9.9 |

## 11. Vegas Pro (MAGIX)

| Feature | Adopt | Where |
|---|---|---|
| Fast, flexible timeline, drag-and-drop everything | Yes (ease of use) | – |
| Event envelopes (opacity/volume curves on clips) | Yes | AUDIT §2.32 |
| Pan/crop tool, track motion | Yes | COLOR §10.1–10.2 |
| AI tools: smart masking, style transfer, upscaling, text-based editing | Yes / Later | COLOR §18 |
| GPU-accelerated effects and encoding on Windows | Yes | AUDIT §10.1, §11.2 |
| Nested projects, multicam | Later | AUDIT §2.9–2.10 |

## 12. Avid Pro Tools

| Feature | Adopt | Where |
|---|---|---|
| Industry DAW: multitrack recording, editing, mixing | Partly — the recording + clean-up subset | AUDIT §3 |
| Elastic Audio (time-stretch), clip gain, automation (volume/pan curves) | Yes (clip gain, volume keyframes) | AUDIT §3.3 |
| Mixing with buses, sends, plug-ins (AAX) | Later | AUDIT §3.14, 3.18 |
| Dolby Atmos mixing, surround | No / Later | AUDIT §3.13 |
| Loudness metering for delivery | Yes | AUDIT §3.5 |
| Video track for post-production sync | Already (Lectern is video-first) | – |

## 13. Adobe Audition

| Feature | Adopt | Where |
|---|---|---|
| Waveform and multitrack editing | Yes (waveforms exist) | AUDIT §3.17 |
| Spectral frequency display, spot healing | Later | AUDIT §3.17 |
| Noise reduction (capture noise print), DeNoise, DeReverb, Adaptive noise | Yes (P0 for recordings) | AUDIT §3.6 |
| Essential Sound panel, auto-ducking, Match Loudness | Yes | AUDIT §3.5, 3.11 |
| Remix (retime music) | Later | AUDIT §3.12 |
| Podcast templates, batch processing | Later | – |

## 14. iZotope RX

| Feature | Adopt | Where |
|---|---|---|
| Dialogue Isolate (voice from background) | Yes (P0) | AUDIT §3.6 |
| Voice De-noise, Spectral De-noise | Yes | AUDIT §3.6 |
| De-reverb (room echo) | Yes (P1) — common in home recordings | AUDIT §3.21 |
| De-click, de-crackle, de-hum (50/60 Hz), de-plosive, de-ess, breath control, mouth de-click | Yes (P1/P2) | AUDIT §3.22 |
| Spectral repair (paint out a noise in the spectrogram) | Later | – |
| Repair Assistant (AI suggests a chain) | Yes — one-click "Clean up voice" | AUDIT §3.23 |
| Loudness control and true-peak limiting | Yes | AUDIT §3.5 |
| Music Rebalance (separate vocals/instruments) | Later | – |

---

## 15. Combined capability matrix

● = a main strength of the tool · ◐ = present · blank = not its job.

| Capability | DR | PR | FCP | Avid MC | AE | Fusion | Nuke | Houdini | C4D | Blender | Vegas | Pro Tools | Audition | RX | Lectern target |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Editing timeline | ● | ● | ● | ● | ◐ | | | | | ◐ | ● | ◐ | ◐ | | ● |
| Color grading | ● | ● | ◐ | ◐ | ◐ | ◐ | ◐ | | | ◐ | ◐ | | | | ● |
| Motion graphics / titles | ◐ | ◐ | ◐ | ◐ | ● | ● | ◐ | ◐ | ● | ◐ | ◐ | | | | ◐ (templates) |
| Compositing / VFX | ◐ | ◐ | | | ● | ● | ● | ● | ◐ | ● | ◐ | | | | ◐ (keying, masks) |
| 3D | ◐ | | | | ◐ | ◐ | ◐ | ● | ● | ● | | | | | – |
| Simulations / particles | | | | | ◐ | ◐ | | ● | ◐ | ● | | | | | – |
| Tracking / roto | ● | ◐ | ◐ | | ● | ● | ● | ◐ | ◐ | ● | ◐ | | | | ● (2D) |
| Audio mixing | ● | ● | ◐ | ◐ | ◐ | | | | | ◐ | ◐ | ● | ● | | ◐ |
| Audio repair | ● | ● | ◐ | ◐ | | | | | | | ◐ | ◐ | ● | ● | ● (voice cleanup) |
| AI transcription / text editing | ● | ● | ● | ◐ | | | | | | | ◐ | | ◐ | | ● |
| Screen + camera recording | | | | | | | | | | | | | | | ● (Lectern core) |
| Collaboration | ● | ● | ◐ | ● | ◐ | | ◐ | | | | | ● | | | ◐ (share links) |

---

## 16. What this means for Lectern

1. **Core identity:** a recorder + editor for tutorials and talking-head
   videos. From the 14 tools Lectern takes the editing of Premiere/Final
   Cut/Resolve, the color of Resolve/Lumetri, the motion-graphics templates
   of After Effects, and the voice cleanup of RX/Audition.
2. **Out of scope:** full 3D (Houdini, Cinema 4D, Blender), feature-film
   compositing (Nuke), enterprise shared storage (Avid). Lectern covers
   their *results* for its audience through templates and presets.
3. **New items found in this pass** (now added to FULL_GAP_AUDIT §2.30–2.32, §3.21–3.24):
   - Connected clips / storylines (Final Cut) — overlays and text that ride
     with a recording segment through ripple edits. P1. Design in
     ../TIMELINE_ENGINE.md.
   - Roles and stem export (Final Cut) — dialogue / music / effects stems.
     P2.
   - Pro trim mode (Avid) — dual-roller trim while playing. P2.
   - De-reverb, de-hum, de-click, de-plosive, breath and mouth-click removal
     (RX). P1/P2 — added to the audio repair scope (AUDIT §3).
   - Repair Assistant style one-click "Clean up voice". P0 together with
     voice isolation.
   - Event envelopes on clips (Vegas) — the same as volume/opacity
     keyframe curves drawn on the clip. P1.
