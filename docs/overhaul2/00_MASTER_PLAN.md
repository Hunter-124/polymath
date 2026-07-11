# Polymath Overhaul 2 — Master Plan

> **Read this first.** This directory is the single source of truth for Overhaul 2
> (2026-07, follows the completed Overhaul 1 in `docs/overhaul/`). It is written so a
> fresh driver session — any harness, any machine, resuming after a usage-limit cut —
> can execute it without re-deriving anything. Execution state = [PROGRESS.md](PROGRESS.md)
> + `git log --grep=overhaul2`. The full DAG with node specs = [01_DAG.md](01_DAG.md).

## 1. Why (owner's brief, 2026-07-10)

Make Polymath something the owner can **actually use every day**:

1. **Fix**: asked the model to open a YouTube video → it failed, AND its answer was shown
   in chat as a raw tool call.
2. **YouTube first-class** (highest priority): the model must be able to *search for,
   select, and display* YouTube videos ad-free inside the GUI.
3. **Full scripted computer use** with safeguards "within reason" so local models can't
   destroy data.
4. **Advisor/supervisor/manager mode** — like Claude coworker, but optimized to *advise
   and supervise* the owner (and others), not do the work itself.
5. **Finalize the agent harness**: subagent orchestration; schedule tasks at set times /
   intervals; take over the GUI window to show things; render web elements / pictures in
   clean bounding boxes alongside info during research.
6. **Better TTS** — less robotic.
7. **Personalities tab**: in-GUI editor, create/destroy.
8. **Chat**: keep drag-scroll AND allow text selection/highlight.
9. **Shopping list**: generalize copy (icon may stay food).
10. General quality: "as high quality and feature rich as it can be."

## 2. Current state (audited 2026-07-10, code-verified — trust this over older docs)

Overhaul 1 (A0–D5) is fully landed: holographic-glass GUI, AgentLoop v2
(goals/plan/reflect), skills, agent sessions, QtWebEngine + adblock + YT clean-mode,
Kokoro TTS, CUDA build. What's actually broken/missing for the brief above:

### Bugs (root causes confirmed)
- **B-LEAK — tool-call JSON leaks into chat.** `runQuick` final answer is generated
  *unconstrained* but with `include_tool_protocol=true` in the system prompt ("respond
  with a SINGLE JSON object…"); only a weak "Do not output JSON" user nudge counteracts
  it (`agent_loop.cpp:934-949`). No post-parse strip; fallback can publish raw constrained
  output (`:868-869`). Fix = node **A1**.
- **B-ROUTE — "open a youtube video" never reaches the command/skill path.**
  `classifyRouteHeuristic` only matches contiguous `"open youtube"`/`"play youtube"`
  (`agent_loop.cpp:716-719`). Fix = **A1**.
- **B-DEADGOAL — `run_skill` creates goals that never run.** It persists goal+steps but
  never calls `executeGoal`; `resumeActiveGoals()` (`agent_loop.cpp:1720`) is defined but
  never called; `AgentSessionEvent` completion never resumes `waiting_agent` goals. Fix =
  **A2**.
- **B-NOSEARCH — no YouTube search→watch capability exists anywhere.** Only a hardcoded
  `/results?q=` page in `slop_mode`. Fix = wave **B**.
- **B-NOGATE — `ToolRiskClass` is metadata only.** Neither dispatch path calls
  `requiresConfirmation` (`agent_loop.cpp:893-919`, `:1307-1335`). Fix = **A4/C1**.
- **B-OPENPAGE — `ui_control open_page` is a no-op** (`SurfaceHost.qml:140-142`). Fix = **A3/E4**.

### Missing capabilities
- No local shell/filesystem/computer-use tools (only delegation to external CLI agents,
  gated by `agents.allowed_dirs`). → wave **C**.
- No time/interval scheduling of *agent goals* (TaskScheduler = idle queue;
  ProactiveEngine = reminders only, with a working RRULE subset). → **D1**.
- No local subagent orchestration (strictly sequential loop; external sessions run in
  parallel but results never rejoin goals). → **A2 + D2**.
- No screen capture / user-activity awareness (vision = cameras only). → **C3, D3**.
- TTS: Kokoro **is** the active engine (Piper is genuinely a fallback) but voice is
  hard-locked `af_sky`, no engine/voice/speed settings key or UI, per-persona voices
  unmapped, naive sentence splitting. → **D4**.
- Chat body is a `Label` — not selectable (`ChatView.qml:104-112`). → **E1**.
- Personalities view read-only; `PersonalityManager` has no write API; persona.json is
  the source of truth with a hot-reload file watcher (helps!). → **E2**.
- Shopping is already generic at schema/model level; only hint copy is food-flavored
  (`ShoppingView.qml:113`). → **E5**.
- Surfaces: tile/stack/split layouts exist and the AI can arrange them; missing a
  markdown/text card, captioned image, YouTube-results picker, research-board composite.
  → **B2, E3**.
- Window: frameless w/ custom chrome; no fullscreen/always-on-top/raise verbs, AI cannot
  drive the window. → **A3 (schema) + E4 (QML)**.

## 3. Decisions (made with the owner — do not relitigate)

1. **YouTube search WITHOUT an API key**: parse `ytInitialData` from the results page /
   use the public `youtubei` Innertube endpoint via the existing `fetch_page`-style HTTP
   stack. No Google API quota dependency. Playback stays in embedded WebEngine with the
   interceptor + clean-mode script (improved), NOT stream extraction (fragile, ToS-risk).
2. **Computer use = first-party local tools** (`fs_read/fs_write/fs_list/run_command/
   app_launch/clipboard/screen_capture`) guarded by a central `SafetyPolicy` +
   risk-gate enforcement + confirmation UX. External-agent delegation remains for heavy
   coding work. Windows-first (this machine), no cross-platform abstraction yet.
3. **Local "subagents" = goal-tree orchestration** (parent goal fans out child goals +
   parallel external sessions, join step aggregates), NOT parallel local inference — one
   GPU, one inference thread. Cheap/menial fan-out goes to external CLI sessions.
4. **Advisor mode = persona + proactive loop + screen awareness**, not a new service:
   scheduled check-ins/briefings (D1), screen_capture + vision description (C3),
   supervision digests of agent sessions, delivered by voice/toast/chat.
5. **TTS stays Kokoro** (already neural, CPU, budget-friendly); fix = expose
   engine/voice/speed config + per-persona voices + better chunking + voice preview UI.
   Only if the owner is still unhappy after D4 do we evaluate new engines (Z-backlog).
6. **Keep the proven execution mechanics from Overhaul 1**: DAG waves, one node = one
   owner = disjoint files within a wave, `overhaul2(<node>):` commits that tick
   PROGRESS.md in the same commit, capture_views PNG verify loop, CLAUDE.md conventions.

## 4. Hardware truth (unchanged)

i7-9750H (6C/12T), 32 GB RAM, **RTX 2070 Max-Q 8 GB VRAM** (sm_75), Windows 10,
~166 GB free. Budget per `docs/overhaul/04_VOICE_RESOURCES.md` §1. Build recipes:
`docs/BUILD.md` + project CLAUDE.md (VS 18 2026, CUDA 13.3 via Ninja, arch 75,
`.ps1` files need UTF-8 BOM).

## 5. Execution & resume protocol (any harness, any time)

1. Read this file, then [01_DAG.md](01_DAG.md).
2. `git -C polymath log --oneline --grep=overhaul2` + [PROGRESS.md](PROGRESS.md) = what's
   done. A node is done only when its acceptance criteria pass and the commit ticking its
   checkbox is on `master`.
3. Execute the next unblocked node(s). Parallel subagents allowed **only** across nodes
   with disjoint file ownership in the same wave (ownership table in 01_DAG §Global rules).
4. Every node lands as ONE commit: `overhaul2(<node>): <summary>` + PROGRESS.md tick.
   Small follow-up fix commits are fine (`overhaul2(<node> follow-up): …`).
5. Verify loop (cheap, no GPU): `cmake --build build/cpu --config Release --target
   capture_views` → run offscreen → read PNGs. Full CUDA build + live e2e only at wave
   boundaries (nodes F1–F3).
6. If interrupted (usage limits, crash): everything needed to resume is this directory +
   git history. No conversation context is required.

## 6. Document map

| Doc | Contents |
|---|---|
| [01_DAG.md](01_DAG.md) | **The monolithic DAG**: waves, nodes, deps, file ownership, per-node specs + acceptance criteria, backlog |
| [PROGRESS.md](PROGRESS.md) | Living checklist — tick + commit as nodes land |
| `../overhaul/*` | Overhaul-1 specs still valid for background (design system, harness v2 concepts, voice budget, agent sessions) |
