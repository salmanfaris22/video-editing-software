# v2 Summary — decisions and plan at a glance

Short version of the v2 documents (2026-10-05). Details are in the linked
files; the full index is README.md.

---

## 1. Features: built in or downloaded? → hybrid

```
┌──────────── Lectern app (installer ~150–250 MB) ───────────┐
│ Recording · timeline · GPU render · audio · export          │
│ ALL effects, transitions, color tools (tiny shaders)        │──▶ never "missing" in a project
│ Basic templates · core LUTs · icons · English               │
│ Pack manager ───────────────────────────────────────────┐   │
└─────────────────────────────────────────────────────────┼───┘
                         first use: "Download 148 MB?"    ▼
            ┌──────── Pack catalog (CDN, signed) ─────────────┐
            │ AI models: transcription, upscale …             │
            │ Look/LUT packs · template packs                 │
            │ Music/SFX/stickers · fonts · UI languages       │
            └──────────────────────┬──────────────────────────┘
                 verify SHA-256 + Ed25519 signature
                                   ▼
                     Pack store on your disk (only what you use)
```

- "All built in" would add several GB (AI models, libraries).
- "Everything one by one" would break projects that use an effect you don't
  have. Effects are tiny, so they always ship.
- Voice cleanup is built in (core to a recorder, small model).
- A project needing a missing pack still opens, shows an "Install" badge, and
  loses nothing.

→ FEATURE_DELIVERY.md

## 2. Architecture and tools

- **New modules:** `render/` (GPU on Qt RHI), `color/` (OpenColorIO),
  `fx/` (effect registry), `ai/` (ONNX Runtime, whisper.cpp), `transcript/`,
  `track/` (OpenCV), `dsp/` (audio effects, libebur128), `packs/`, `mcp/`.
- **Frame pipeline:** hardware decode → input color transform → per-clip
  correctors and effects → compositing in linear float → display/output
  transform → preview or export; scopes computed from the graded frame.
- **Audio pipeline:** clip cleanup and EQ → tracks → dialogue/music/effects
  buses → auto-ducking → loudness and limiter → device or export.
- **Project format v3:** several correctors per clip, masks, transitions,
  adjustment layers; automatic migration from v2.
- **All dependencies have licences that allow selling the app** (BSD, MIT,
  Apache, LGPL); verify each before adoption.

→ ARCHITECTURE_V2.md

## 3. AI assistants through MCP (Claude, ChatGPT, Codex)

- Lectern is an **MCP server**. Claude Desktop, Claude Code and Codex start
  the small `lectern-mcp` program, which talks to the running app on the
  same computer (or opens the project headless if the app is closed).
- **ChatGPT** runs in the cloud, so it needs an optional online relay with
  sign-in; the relay is off by default.
- **About 35 tools**, all using the same commands as the app's buttons, so
  every AI edit is undoable (⌘Z, labelled "Assistant: …"): timeline,
  cutting/trimming/moving, layers, pauses, text, captions, color, LUTs,
  effects, audio, export jobs with progress, frame images.
- **Workflows:** "Clean up recording", "Make a short", "Add chapters".
- **Safety:** off until enabled per assistant; local connection with a key;
  read-only / edit / full levels; recording, sharing and overwriting files
  always need confirmation in Lectern; "Claude is editing…" indicator with a
  log; one button to undo all assistant edits.
- Setup snippets for each client and testing with MCP Inspector.

→ AI_ASSISTANTS_MCP.md

## 4. Roadmap in small phases

Each step lists its goal, what ships, its MCP tools, tests, "done when", and
size (S = 1–2 weeks, M = 3–4, L = 5–8 for one engineer).

**Phase 1 — DaVinci-style color (14 steps):**
1. GPU rendering
2. High-precision color and color management
3. Several color corrections per clip
4. Resolve color wheels
5. Scopes
6. Curves
7. Select by color (qualifier)
8. Shape masks (windows)
9. Tracking
10. AI subject mask
11. Looks and saved grades
12. Shot matching
13. Simple node view
14. Full Color workspace

**Phase 2 — deep MCP and an "experienced editor" AI (12 steps):**
1. MCP connection
2. Every feature as an AI tool
3. The AI can see and hear (frames, scopes, loudness, transcript, shot analysis)
4. Edit plans: propose → you approve → apply as one undo step
5. Cinematic color (7 styles, skin tones protected, shots matched)
6. Speed ramps
7. Smooth transitions (whip, zoom, smooth jump cuts, layout changes)
8. Focus (punch-in, follow the cursor, vertical reframe, background blur)
9. Editor recipes (pacing, hook, B-roll, music ducking, chapters, captions, voice polish, shorts)
10. Self-review: the AI checks and fixes its own edit
11. One-click "Pro edit" inside Lectern (optional)
12. Quality benchmark on 20 reference recordings

**Rules:** every feature ships with its MCP tool and a test that calls it.
Phase 2 can start alongside Phase 1 once step 1.3 is done (the project
format is stable then).

**Size:** each phase about 9–12 months for one engineer; the first useful
Phase 2 core (2.1–2.4) about 3 months. Estimates only.

→ ROADMAP_PHASES.md

**Every feature in a phase:** MASTER_PHASE_PLAN.md places all 668 open
checklist items into 96 small phases across Stages 0–8 (ship and recorder ·
color · MCP and agent · effects · editing and audio · pro UI · delivery ·
platform · specialist). Build every **a** phase (P0/P1) first, then the
**b** phases (P2), then Stage 8 (P3).

## 5. Claude token estimate

Rough budget for building with Claude Code, in tokens processed (mostly
cached project context; Claude's own output ≈ 5 %); treat as ×0.5–×2:

| Scope | Estimate |
|---|---|
| ROADMAP Phase 1 (color, 14 steps) | ~72M |
| ROADMAP Phase 2 (MCP and agent, 12 steps) | ~60M |
| Every checklist item, Wave 1 (must-haves) | ~250M |
| Every checklist item, all stages | ~430M |

→ ROADMAP_PHASES.md (per step), MASTER_PHASE_PLAN.md (per phase)

## 6. Brand

Logo and app icon (dark): bold periwinkle "L" whose foot is cut into
timeline clips, with a glowing red record dot. Files in `assets/brand/`;
macOS `.icns` and Windows `.ico` are wired into the build; the logo is on
the home screen.

## 7. Where we are and the next step (2026-10-06)

Done: Phase 1.1 GPU rendering, 1.2 input color (HDR tone mapping), the
Resolve-style Color page (1.4 primaries, 1.5 scopes, 1.6 custom curves,
1.7 qualifier, 1.11 looks / LUTs / gallery), nodes that grade part of the
picture (windows, color key, person / background), film effects (grain,
halation, glow, print emulation), before / after compare, grade copy, and
Phase 2.1–2.2 MCP. Lectern now covers about 30 % of Resolve's color
features fully, 36 % counting partial ones (COLOR_GRADING_COMPARISON.md).

Next, in order: HSL curves; sharpen and noise reduction; tracking for
windows; grade keyframes; then the float linear pipeline with ACES
(COLOR_GRADING_COMPARISON.md §5).
