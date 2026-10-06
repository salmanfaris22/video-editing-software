# docs/v2 — Lectern pro-editor plan

Version 2 planning documents (2026-10-05). They describe where Lectern must
go to reach the level of DaVinci Resolve, Premiere Pro, After Effects and the
other industry tools, and what exists today. The original design documents
(ARCHITECTURE, TIMELINE_ENGINE, RENDERING_PIPELINE, …) stay in `docs/`.

| Document | What it covers | Items | Lectern has (full / partial) |
|---|---|---|---|
| [SUMMARY.md](SUMMARY.md) | **Start here:** all decisions and the plan on one page (delivery, architecture, MCP, phases, next step) | – | – |
| [INDUSTRY_TOOLS_FEATURES.md](INDUSTRY_TOOLS_FEATURES.md) | Main features of the 14 industry tools (Resolve, Premiere, Final Cut, Avid, After Effects, Fusion, Nuke, Houdini, Cinema 4D, Blender, Vegas, Pro Tools, Audition, RX) and which Lectern adopts | overview | – |
| [COLOR_EFFECTS_PARITY.md](COLOR_EFFECTS_PARITY.md) | Color pipeline, grading, curves, secondaries, scopes, LUTs, repair, effects, keying, compositing, transitions, time, motion, text, AI | 230 | 28 / 27 |
| [PRO_INTERFACE_PARITY.md](PRO_INTERFACE_PARITY.md) | After Effects-style workspace: tools, viewer, panels, Effect Controls, timeline switches, graph editor, Character panel, menus | 203 | 28 / 45 |
| [DAVINCI_COLOR_PAGE.md](DAVINCI_COLOR_PAGE.md) | Resolve 21 Color page control by control (from screenshots) and the plan for a Lectern Color workspace | 104 | 26 / 10 |
| [FULL_GAP_AUDIT.md](FULL_GAP_AUDIT.md) | Everything else: media, editing, audio, motion graphics, delivery, captions, AI, collaboration, project, performance, platform, recording, privacy, shipping; icon inventory (249 new icons); one combined priority order | 199 | 18 / 42 |
| [SHORTCUTS.md](SHORTCUTS.md) / [SHORTCUTS.pdf](SHORTCUTS.pdf) | Lectern shortcuts today, planned keymap, AE/PR/DR/FCP reference keymaps | – | – |
| [ARCHITECTURE_V2.md](ARCHITECTURE_V2.md) | How to build it: v2 module map, GPU frame pipeline, audio pipeline, threads, data model v3, best libraries (with licences), 6 implementation phases with tests, risks — 9 diagrams | – | – |
| [FEATURE_DELIVERY.md](FEATURE_DELIVERY.md) | Built in vs downloaded features: **hybrid** decision, size budget, pack format, signed catalog, install flow, pack states, missing-pack handling — 4 diagrams | – | – |
| [AI_ASSISTANTS_MCP.md](AI_ASSISTANTS_MCP.md) | Connect Claude Desktop, Claude Code, OpenAI Codex and ChatGPT to Lectern as an MCP server: architecture, ~35 tools/resources/prompts mapped to existing commands, safety and consent, client setup snippets, build steps — 3 diagrams | – | – |
| [ROADMAP_PHASES.md](ROADMAP_PHASES.md) | **The order of work in small phases.** Phase 1: DaVinci-style color grading in 14 steps. Phase 2: deep MCP and a pro-editor agent in 12 steps (cinematic color, speed ramps, smooth transitions, focus, editor recipes, self-review) — 3 diagrams | – | – |
| [MASTER_PHASE_PLAN.md](MASTER_PHASE_PLAN.md) | **Every feature, phase by phase:** all open checklist items (636 open as of 2026-10-06) placed into small phases (Stages 0–8, each phase split into must-have **a** / power-user **b**, specialist P3 last); each item exactly once; Claude token estimate per phase and stage; regenerate with `python3 scripts/build_master_plan.py` | 636 open | – |

**Total: 736 checklist items — 100 done, 124 partly done, 512 missing** (updated 2026-10-06: GPU renderer, MCP, input color, Color page with Resolve primaries and scopes, menu bar).

**Delivery decision:** the engine and every effect, transition and color tool
are built in (small, never missing from a project); AI models, look/LUT
libraries, templates, stock media, fonts and languages are packs downloaded
one by one on first use (FEATURE_DELIVERY.md).

Where to start: **ROADMAP_PHASES.md** (small phases, in order), backed by
FULL_GAP_AUDIT.md §14 (priorities) and ARCHITECTURE_V2.md §8. The first
engineering step is the GPU float, color-managed pipeline
(COLOR_EFFECTS_PARITY.md §1, Phase C0); nearly every color and effect item
depends on it.

Conventions in all v2 documents: ✅ done · 🟡 partly · ⬜ missing;
● / ◐ / – for what the reference apps have; P0 (first) … P3 (specialist).
Reference-app details come from general product knowledge and must be
verified against vendor manuals before each item is built; ✅/🟡 statuses
were checked against Lectern's source.

Diagrams are Mermaid (render on GitHub and most IDEs; key ones also have an
ASCII copy). Regenerating the shortcut PDF: convert SHORTCUTS.md to HTML and print it with
headless Chrome (`--headless=new --print-to-pdf=SHORTCUTS.pdf`).
