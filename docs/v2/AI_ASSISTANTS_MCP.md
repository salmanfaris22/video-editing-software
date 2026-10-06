# AI Assistants — connect Claude, ChatGPT and Codex to Lectern (MCP)

Goal: let AI assistants operate Lectern — "cut the pauses, add captions, put
a title at 0:05, export a 9:16 version" — by exposing Lectern as an **MCP
server** (Model Context Protocol). One server works with every MCP client:

| Client | How it connects | Notes |
|---|---|---|
| **Claude Desktop** | Local, stdio (`lectern-mcp` started by Claude) | Config file entry, §6.1 |
| **Claude Code** (CLI, desktop, IDE) | Local, stdio or local HTTP | `claude mcp add`, §6.2 |
| **OpenAI Codex** (CLI / IDE extension) | Local, stdio | `~/.codex/config.toml`, §6.3 |
| **ChatGPT** (web/desktop, connectors / developer mode) | **Remote** HTTPS MCP endpoint | Needs a relay because ChatGPT runs in the cloud, §6.4 |
| Any other MCP client (Cursor, VS Code, Gemini CLI …) | stdio or HTTP | Same server |

Status (2026-10-07): **built and verified with real clients** — 49 tools,
2 resources, 2 prompts; the in-app endpoint with per-assistant approval;
the `lectern-mcp` stdio bridge; a headless mode. Checked end to end with
the official Python SDK (2.3.0, both handshake and "auto" mode), the
official MCP Inspector CLI (TypeScript SDK) and Claude Code 2.1.291 —
see §6.6. Codex, Claude Desktop and ChatGPT were not run here (unverified).
Client setup steps change often; verify against each vendor's current MCP
documentation. Protocol: JSON-RPC 2.0 over stdio (bridge) and
Streamable HTTP on 127.0.0.1 (app); handshake revisions 2024-11-05,
2025-03-26, 2025-06-18 and 2025-11-25.

---

## 1. Why MCP

- **One integration, every assistant.** Claude, ChatGPT, Codex and IDE agents
  all speak MCP; no per-vendor plug-in.
- **The assistant uses the same commands as the UI.** Every MCP tool calls
  the existing command layer (`ProjectController`, ~80 `Q_INVOKABLE`
  commands; `timeline::edit` operations), so assistant edits are validated,
  undoable (⌘Z) and saved like the user's own.
- **Local and private by default.** The assistant talks to the Lectern on
  the user's machine; media never leaves it unless the user exports or
  shares.
- **Works headless too.** The same tools can run on a project without the
  GUI (like `lectern-export` today) for batch jobs and CI.

---

## 2. Architecture

```mermaid
flowchart LR
    subgraph CLIENTS["AI clients"]
        CD["Claude Desktop"]
        CC["Claude Code"]
        CX["OpenAI Codex CLI"]
        GPT["ChatGPT (cloud)"]
    end
    subgraph LOCAL["User's computer"]
        BR["lectern-mcp (stdio bridge executable)"]
        subgraph APP["Lectern app"]
            HTTP["MCP endpoint: Streamable HTTP on 127.0.0.1 (token)"]
            REG["mcp/ tool registry: schemas, validation, consent"]
            CMD["Command layer: ProjectController, PlaybackController, ExportController, RecorderController"]
            ENG["Engine: timeline edit ops, render, export"]
        end
        HEAD["Headless mode: lectern-mcp --project dir (no GUI)"]
    end
    RELAY["Optional relay: HTTPS + OAuth (off by default)"]

    CD -->|"stdio"| BR
    CC -->|"stdio or http"| BR
    CX -->|"stdio"| BR
    BR -->|"local socket + token"| HTTP
    CC -.->|"http, localhost"| HTTP
    GPT -->|"HTTPS"| RELAY
    RELAY -->|"outbound tunnel from the app"| HTTP
    HTTP --> REG
    REG --> CMD
    CMD --> ENG
    BR -.->|"app not running"| HEAD
```

Terminal version:

```
 Claude Desktop ─┐
 Claude Code ────┼─ stdio ─▶ lectern-mcp ─┐ local socket + token
 Codex CLI ──────┘                        ▼
                         ┌──────────── Lectern app ────────────┐
 Claude Code ── http ──▶ │ MCP endpoint 127.0.0.1:<port>/mcp   │
                         │   ▼                                 │
 ChatGPT ─ HTTPS ─▶ relay ─ tunnel ─▶ tool registry (schemas,  │
   (optional, off)       │   consent) ▼                        │
                         │ ProjectController … (same as UI)    │
                         │   ▼                                 │
                         │ timeline edit ops · render · export │
                         └─────────────────────────────────────┘
 lectern-mcp --project <dir>  → headless engine when the app is closed
```

Components:

| Part | Where | Role |
|---|---|---|
| `src/mcp/` (Qt-free) | new module | JSON-RPC 2.0 + MCP message handling, tool/resource/prompt registry, JSON Schemas, argument validation, result formatting |
| MCP endpoint in the app | `src/ui/McpController` | Streamable HTTP server bound to `127.0.0.1` (reuses `src/network/HttpServer`), bearer token, dispatches tools to controllers on the UI thread |
| `lectern-mcp` | `tools/lectern-mcp` | Small executable that clients start. Speaks MCP over stdio and relays to the running app (URL and token from a user-only connection file). Starts before Lectern does (answers the handshake and the tool list itself, and tells the assistant how to turn access on), opens a new session when Lectern restarts or the user ends the old one, ends its session when the assistant quits (also on SIGTERM). Never exits on a failed request. `--project DIR [--allow-edits]` serves one project without the app; each edit is on disk before its reply |
| Consent UI | QML | `AssistantRequest.qml`: a prompt on the window when an assistant connects (Read only · Read and edit · Deny); Assistants menu and `AssistantsDialog.qml`: on/off, connected clients, revoke, deny, undo an assistant's edits, activity log |
| Relay (optional) | Lectern cloud service | Gives ChatGPT a public HTTPS URL with OAuth; the app opens an outbound tunnel; disabled by default |

Implementation language: MCP has official SDKs for TypeScript, Python, C#,
Java, Kotlin, Go, Rust and others but not C++; the protocol is small
JSON-RPC, so `src/mcp/` implements it directly with nlohmann/json (already a
dependency) and is tested against the official **MCP Inspector** and the
conformance behaviour of the clients in §6.

---

## 3. Tools (what the assistant can do)

Every tool maps to existing or planned commands. Edits are one undo step
each, labelled "Assistant: …" in the undo history.

### 3.1 Project and timeline (read)

| Tool | Arguments | Returns | Backed by |
|---|---|---|---|
| `list_projects` | – | recent projects (title, path, duration) | home screen list |
| `open_project` | `path` | project summary | `ProjectController::open` |
| `get_timeline` | `detail?` | tracks, clips (id, start, duration, role, name), markers, layout regions | `tracks`, `markers`, `layoutRegions` views |
| `get_selection` | – | selected clips and layer | `selection`, `selectedTrack` |
| `render_frame` | `time`, `width?` | PNG image (MCP image content) | `lectern-export --frame` path |
| `get_transcript` | `from?`, `to?` | words with times | transcript module (planned) |

### 3.2 Editing

| Tool | Arguments | Backed by |
|---|---|---|
| `split_at` | `time`, `clipId?` | `splitAt` |
| `delete_clips` | `clipIds[]`, `mode` (gap / ripple) | `deleteSelected` with `linkedEditMode` |
| `remove_range` | `start`, `end` | `removeRange` |
| `trim_clip` | `clipId`, `edge`, `time` | `trimClip` |
| `move_clip` | `clipId`, `time`, `layerId?` | `moveClip`, `moveClipToTrack` |
| `find_pauses` / `remove_pauses` | `thresholdDb`, `minPause`, `padding` | `findSilences`, `removeSilences` |
| `add_marker` | `time`, `label` | `addMarker` |
| `set_layout` | `preset`, `from?` | `setLayoutPreset`, `setLayoutFrom` |
| `add_layer` / `rename_layer` / `move_layer` / `delete_layer` | … | `addTrack`, `renameTrack`, `moveTrack`, `deleteTrack` |
| `undo` / `redo` | `steps?` | `undo`, `redo` |

### 3.3 Text, captions, look, audio

| Tool | Arguments | Backed by |
|---|---|---|
| `add_text` | `text`, `time`, `duration`, `preset`, `style?` | `addText`, `setTextValue` |
| `add_captions` | `language?`, `style?` | transcription (planned) → subtitle track |
| `import_subtitles` / `export_subtitles` | `path` | `importSubtitles`, `exportSubtitles` |
| `set_color` | `clipId` or `role`, values (exposure, contrast, saturation, temperature, tint, wheels) | `setColorValues`, `setColorWheel`, `applyToRole` |
| `apply_lut` | `clipId`, `lut`, `amount` | `setColorLut` |
| `list_looks` / `apply_look` | `clipIds`, `look` (oppenheimer, dark-green, teal-orange …, or "none"), `amount` | `looks`, `applyLook` — a look on top of the clip's correction |
| `set_hsl_curve` | `clipId`, `nodeId?`, `curve` (hueVsHue, hueVsSat, hueVsLum, lumVsSat, satVsSat, satVsLum), `points` | `setHslCurve`, `setNodeHslCurve` — e.g. calm the greens |
| `copy_grade` | `fromClipId`, `toClipIds` | `copyGradeTo` (the user's copied grade is untouched) |
| `add_node` | `clipId`, `select` (whole, person, background, circle, rectangle, gradient, color), `window?`, `qualifier?`, `pick? {x, y, time}`, `grade?`, `invert?` → `nodeId` | `addNode`, `setNodeWindow`, `setNodeQualifier`, `pickNodeColor`, `setNodeValue` — grade only part of the picture |
| `set_node` / `remove_node` | `clipId`, `nodeId`, any of the above, `enabled`, `label` | node setters, `removeNode` |
| `set_effect` | `clipId`, `type` (blur, vignette, zoom, background-blur, film-grain, glow, halation, film-emulation, denoise, sharpen), `params` (denoise: luma, chroma, radius; sharpen: amount, radius, coring) | `setEffectEnabled`, `setEffectValue` |
| `set_style` | key/value (padding, radius, camera shape …) | `setStyleValue` |
| `set_audio` | `clipId` or `trackId`, gain/mute/fades | `setClipAudio`, `setTrackValue` |
| `clean_voice` | `clipId?` | voice cleanup (planned, FULL_GAP_AUDIT §3.23) |
| `add_music` | `path`, `time` | `importMedia(..., "music")` |

### 3.4 Export and recording

| Tool | Arguments | Notes |
|---|---|---|
| `export_video` | `path`, `preset` (youtube, shorts-9x16, …), `range?` | Long-running: returns a job id; progress via MCP progress notifications; `get_job` / `cancel_job` |
| `start_recording` / `stop_recording` | sources, countdown | **Always needs on-screen confirmation in Lectern** (§5) |

### 3.5 Resources and prompts

| Kind | URI / name | Content |
|---|---|---|
| Resource | `lectern://project/current` | project summary (JSON) |
| Resource | `lectern://project/timeline` | full timeline view (JSON) |
| Resource | `lectern://project/transcript` | transcript (text + timings) |
| Resource | `lectern://frame/{seconds}` | PNG frame |
| Resource | `lectern://docs/shortcuts` | SHORTCUTS.md (helps the assistant explain the UI) |
| Prompt | `clean_up_recording` | remove pauses + filler words, clean voice, add captions |
| Prompt | `make_short` | pick the best 30–60 s, reframe 9:16, captions, export |
| Prompt | `add_chapters` | chapters and markers from the transcript |

Tool annotations (MCP `readOnlyHint`, `destructiveHint`, `idempotentHint`)
are set on every tool so clients can auto-approve read-only tools and ask
for edits.

---

## 4. Example: "remove the pauses and add a title"

```mermaid
sequenceDiagram
    actor U as User
    participant A as Claude / Codex / ChatGPT
    participant M as lectern-mcp
    participant L as Lectern app
    participant P as ProjectController

    U->>A: "Remove the long pauses and add the title Intro to Qt at the start"
    A->>M: tools/call get_timeline
    M->>L: forward (token)
    L->>P: read views
    P-->>A: tracks, clips, duration 8:42
    A->>M: tools/call find_pauses {thresholdDb: -42, minPause: 0.7}
    M->>L: forward
    L->>P: findSilences (async)
    P-->>A: 37 pauses, 1:58 total
    A->>M: tools/call remove_pauses
    L->>P: removeSilences (one undo step "Assistant: Remove pauses")
    P-->>A: new duration 6:44
    A->>M: tools/call add_text {text: "Intro to Qt", time: 0, preset: "title"}
    P-->>A: clip id
    A->>M: tools/call render_frame {time: 1}
    L-->>A: PNG preview
    A-->>U: "Done: removed 1:58 of pauses, added the title (preview attached). Undo with Cmd+Z."
```

---

## 5. Safety, consent and privacy

| Rule | Detail |
|---|---|
| Off until enabled | Assistants → "Allow AI Assistants"; off again when Lectern quits |
| Local only by default | HTTP endpoint binds to `127.0.0.1`; random port written to a user-only file (0600) with a random 64-character bearer token; Host and Origin are checked; the stdio bridge reads the file |
| Approval per connection | each new session waits for the user: a prompt on the window offers *Read only* · *Read and edit* · *Deny*; until then every tool call answers "needs approval". *Deny* also refuses that assistant's new sessions until access is turned off and on. At most 16 sessions; the oldest unapproved one makes room |
| Approval levels | *Read only* · *Edit (undoable)* (built); *Full (export, import files, recording)* planned |
| Always confirm in Lectern | starting a recording, overwriting files outside the project, deleting layers with clips, sharing/uploading |
| Undoable | each tool call is one labelled undo step; "Undo all assistant edits since …" button |
| Visible | top-bar chip "Claude is editing…" with an activity log of every call |
| File access | paths limited to the project folder and folders the user approved; no arbitrary file reads |
| Media privacy | frames and transcripts are only returned when a tool asks for them; the client then sends them to its model provider — shown in the consent text |
| Remote (ChatGPT) | relay off by default; OAuth sign-in; per-session approval in the app; relay never stores media |
| Rate limits | long jobs queued; one export at a time |

---

## 6. Client setup

The paths below assume the macOS app bundle; on Windows use
`C:\Program Files\Lectern\lectern-mcp.exe`; in a development build the
bridge is `build/dev/bin/lectern-mcp`. Assistants → "Copy Setup for
Claude / Codex" copies the JSON below with the correct path.

Steps for every client: (1) add the server as below; (2) in Lectern turn
on Assistants → Allow AI Assistants; (3) when the assistant connects,
answer Lectern's prompt (Read only or Read and edit). The order of 1 and 2
does not matter: the bridge waits for Lectern.

### 6.1 Claude Desktop

`claude_desktop_config.json` (Claude → Settings → Developer → Edit Config):

```json
{
  "mcpServers": {
    "lectern": {
      "command": "/Applications/Lectern.app/Contents/MacOS/lectern-mcp",
      "args": []
    }
  }
}
```

Restart Claude Desktop; Lectern's tools appear in the tools menu.
(A one-click desktop extension package can replace manual config later.)

### 6.2 Claude Code

```bash
# stdio (recommended)
claude mcp add lectern -- /Applications/Lectern.app/Contents/MacOS/lectern-mcp

# without the app, one project (read only unless --allow-edits)
claude mcp add lectern-project -- /Applications/Lectern.app/Contents/MacOS/lectern-mcp --project ~/Movies/MyRecording
```

Check with `claude mcp list`, or `/mcp` inside a session. A project-scoped
`.mcp.json` can be committed to share the setup with a team. (The app's
HTTP endpoint is not meant to be added directly: its port and token change
every time access is turned on; the bridge follows them.)

### 6.3 OpenAI Codex (CLI / IDE extension)

`~/.codex/config.toml`:

```toml
[mcp_servers.lectern]
command = "/Applications/Lectern.app/Contents/MacOS/lectern-mcp"
args = []
```

(Newer Codex versions also offer `codex mcp add`; check `codex mcp --help`.)

### 6.4 ChatGPT

ChatGPT runs in the cloud, so it can only reach a **public HTTPS** MCP
server. Two options:

1. **Lectern relay (planned):** Settings → AI assistants → ChatGPT → "Create
   connection" gives a URL like `https://mcp.<lectern-domain>/u/<id>`; in
   ChatGPT add it as a custom connector (developer mode / connectors
   settings), sign in with OAuth, and approve the session in Lectern.
2. **Self-hosted tunnel** for developers: expose the local endpoint through
   a tunnel (e.g. Cloudflare Tunnel) with authentication. Not recommended
   for normal users.

Availability of custom connectors depends on the ChatGPT plan and settings
at the time; verify in OpenAI's documentation.

### 6.5 Testing with MCP Inspector

```bash
# interactive (browser UI)
npx @modelcontextprotocol/inspector /Applications/Lectern.app/Contents/MacOS/lectern-mcp
# scripted: pass the server through a config file (the CLI swallows server flags such as --project)
npx -y @modelcontextprotocol/inspector --cli --config lectern.json --server lectern --method tools/list
npx -y @modelcontextprotocol/inspector --cli --config lectern.json --server lectern \
    --method tools/call --tool-name set_color --tool-arg clipId=<id> --tool-arg 'values={"exposure":0.3}'
```

Lists the tools, calls them by hand, and shows raw JSON-RPC — used in
development and in the release checklist.

### 6.6 Verified clients and how to re-run the checks

| Client | Mode | Checked (2026-10-07) |
|---|---|---|
| Official Python SDK 2.3.0, `ClientSession` | headless | handshake (2025-11-25); all 49 input schemas valid JSON Schema 2020-12; get_timeline, set_effect, list_looks, apply_look, add_node, get_scopes, render_frame (PNG image content); -32602 on a bad enum; tool error on an unknown clip; resources list/read; prompts list/get; ping; `server/discover` → -32601 |
| Official Python SDK 2.3.0, `Client` ("auto": `server/discover` probe, then handshake) | app, through the bridge | connects (2025-11-25), 49 tools, waits for approval, add_marker lands in the app's project |
| MCP Inspector CLI (TypeScript SDK) | headless | tools/list, tools/call (get_project, set_color saved to disk with the undo label "Assistant: set_color", render_frame PNG), resources/list, prompts/list |
| Claude Code 2.1.291 (`claude -p --mcp-config … --strict-mcp-config`, Haiku) | headless | finds the Screen clip, enables Sharpen; the edit is on disk |
| Claude Code 2.1.291 | app, through the bridge | approved as "claude-code", add_marker completed in the app |

Automated (every build): `tests/unit/mcp/ServerTest.cpp` (protocol),
`tests/ui/McpTest.cpp` (tools, HTTP consent), `tests/ui/McpBridgeTest.cpp`
(the real `lectern-mcp` process against the app's endpoint: starting before
Lectern, approval levels, Lectern restarting, revoke, deny, session end on
quit and on SIGTERM, probes and bad lines, headless edits surviving a
SIGKILL), `tests/qml/AssistantsTest.cpp` (the approval prompt).

Any real client against the app endpoint (the test plays the user and
approves):

```bash
# official Python SDK
LECTERN_TEST_MCP_CLIENT="uv run -q --no-project --with mcp python '$PWD/tests/mcp/sdk_client.py'" \
  build/dev/bin/lectern_ui_tests --gtest_filter='McpBridge.RealClient*'
# Claude Code
LECTERN_TEST_MCP_CLIENT='claude -p "Add a marker named real-client at 1 s with the lectern tools." \
  --mcp-config "$LECTERN_TEST_MCP_CONFIG" --strict-mcp-config --allowedTools mcp__lectern__add_marker < /dev/null' \
  build/dev/bin/lectern_ui_tests --gtest_filter='McpBridge.RealClient*'
```

Problems found and fixed by these runs: headless edits could be lost when
the assistant ended the bridge (the save timer never ran while the bridge
waited on stdin); the bridge left sessions behind (16 later, new assistants
were refused), exited on Lectern restarting or on the newer clients'
`server/discover` probe, and could not start before Lectern; no prompt told
the user an assistant was waiting; 2024-11-05 clients were offered another
revision.

### 6.7 Troubleshooting

| Symptom | Cause and fix |
|---|---|
| Tool calls answer "Lectern isn't reachable" | Lectern is closed or Assistants → Allow AI Assistants is off. Turn it on; the next call connects |
| Tool calls answer "needs approval" | Answer the prompt in Lectern's window (or Assistants → Manage Assistants…) |
| "You denied this assistant access" | Turn Allow AI Assistants off and on, then approve it |
| "Lectern did not answer in time" | Lectern was busy for 30 s; the request may have run — check the project before retrying |
| Headless: "Disable the app's assistant endpoint…" | The app has access on; edit through the app, or turn it off and close the project first |

---

## 7. Lectern as an MCP client (later)

The reverse direction: an in-app assistant panel ("Ask Lectern") that uses a
model provider the user chooses (Anthropic Claude API, OpenAI API, or a local
model) and can call **other** MCP servers the user connects (stock media
library, Google Drive, Notion scripts …). It would use the same tool
registry internally. Out of scope until the server side ships; listed in
FULL_GAP_AUDIT §7 as AI features.

---

## 8. How to build it

```mermaid
flowchart TB
    S1["1. src/mcp: JSON-RPC 2.0, initialize, tools/list, tools/call, resources, prompts, progress, cancellation"] --> S2
    S2["2. Tool registry: JSON Schema per tool, maps to ProjectController commands, undo labels"] --> S3
    S3["3. McpController in the app: localhost Streamable HTTP via network/HttpServer, token file"] --> S4
    S4["4. tools/lectern-mcp: stdio bridge, headless fallback with --project"] --> S5
    S5["5. Consent UI, activity log, approval levels, undo-all"] --> S6
    S6["6. Client snippets in Settings; test with MCP Inspector, Claude Code, Codex, Claude Desktop"] --> S7
    S7["7. Optional relay with OAuth for ChatGPT"]
```

| Step | Files | Tests |
|---|---|---|
| 1 | `src/mcp/JsonRpc.*`, `src/mcp/Server.*` | unit: message parsing, errors, batching rules, cancellation |
| 2 | `src/mcp/Tools.*` (schemas), `src/ui/McpTools.cpp` (bindings) | controller tests per tool (same pattern as `tests/ui/ControllerTest.cpp`); schema validation rejects bad input |
| 3 | `src/ui/McpController.*` | integration: HTTP call to localhost changes the timeline; wrong token refused |
| 4 | `tools/lectern-mcp/main.cpp` | spawn the bridge in a test, run `initialize` + `tools/list` + `get_timeline` over stdio (done: `tests/ui/McpBridgeTest.cpp`) |
| 5 | QML settings page + top-bar chip | QML interaction tests (`tests/qml/`) |
| 6 | docs + snippets | manual checklist with each client; MCP Inspector run in CI |
| 7 | relay service (separate repo) | security review, OAuth flow tests |

Done when: from Claude Code, Codex and Claude Desktop, a user can ask "remove
the pauses, add captions and export for YouTube" on a real recording; every
change is undoable in the app; nothing runs without the user having enabled
the client.
