# Polymath Overhaul — Master Plan

> **Read this first.** This directory is the single source of truth for the 2026-07 overhaul.
> It was authored by a planning session (Claude Fable) after a full codebase + hardware audit,
> and is written so that a fresh driver session (Claude Opus, or any capable harness on any
> machine) can execute it without re-deriving anything. Execution state lives in
> [PROGRESS.md](PROGRESS.md) — update and commit it as nodes complete.

## 1. Vision

Polymath becomes a **Jarvis / Star Trek-computer**: an always-on, voice-first, local AI
presence for the home that
- **enhances daily life** (reminders, planning, memory, shopping, briefings),
- **runs a real agentic harness** (multi-step goals that persist across turns and restarts,
  plan → execute → reflect, background execution with results delivered back), not a chat
  box with leaf tools,
- **delegates hard work to external AI CLI agents** (Claude Code first; Codex, Grok, etc. via
  adapters) — "a more transparent cowork": Polymath spawns/monitors headless sessions,
  shows them as live cards, notifies by voice/toast when they need input, and relays replies,
- **composes its own screen** ("put YouTube slop on and tell me when Claude needs me"): the
  AI can spawn/arrange content surfaces (web, video, dashboards, session monitors) via a
  `ui_control` tool + SurfaceHost,
- looks the part: a **holographic liquid-glass GUI** (frameless window, aurora wallpaper,
  per-section color coding, glass components) replacing the flat Tokyo Night theme.

Modularity rule: capabilities ship as **skills** (declarative, AI-authorable workflow
bundles) and **surfaces**, not hardcoded features.

## 2. Hardware truth (this machine — budget everything against it)

| Resource | Value |
|---|---|
| CPU | Intel i7-9750H, 6C/12T (laptop) |
| RAM | 32 GB (≈22 GB typically free) |
| GPU | **RTX 2070 Max-Q, 8 GB VRAM** (sm_75); ~0.7 GB used by Windows at idle |
| Disk | ≈166 GB free on C: |
| Use profile | Machine is dedicated to Polymath + YouTube + web research (no gaming/render contention) |

The docs previously assumed a 12 GB RTX 3080 Ti — **that is wrong for this machine.**
Resource budget: see [04_VOICE_RESOURCES.md](04_VOICE_RESOURCES.md) §1.

## 3. Decisions (made with the owner — do not relitigate)

1. **Architecture**: local harness rebuild **+ external agent delegation** (adapter-based
   session manager). Local model = voice, routing, quick answers, summarization, skill
   orchestration. External CLI agents = coding, deep research, OS-level computer use.
2. **Parked** (code stays, feature deprioritized, no DAG work beyond light restyle):
   - Heavy 27B local model path (unusable on 8 GB; deep reasoning → delegation or idle-queue)
   - Mobile gateway + Capacitor app
3. **Kept**: cameras/vision pipeline, personalities system.
4. **Models**: middle strategy — Fast model comfortably resident (4k ctx, q8 KV), whisper GPU
   on-demand, ~1.5–2 GB VRAM headroom preserved for browser/video decode.
5. **GUI**: desktop QML only this pass; hybrid glass (real MultiEffect blur on hero surfaces,
   faux-glass everywhere else, global effects toggle); brand-new "holographic aurora" palette
   (near-black base, electric-cyan primary, per-section hues); frameless window + icon nav;
   command palette; settings page; notification center; dashboard HUD; SurfaceHost.
6. **QtWebEngine is NOT installed** in the Qt 6.6.3 kit. Web surfaces ship as a graceful
   placeholder now; installing QtWebEngine (aqt module or Qt maintenance) is a separate,
   explicit later step (see 06_DAG node D5-optional).

## 4. Current-state audit (what's broken/missing — verified 2026-07-10)

**Harness** (details in [03_HARNESS.md](03_HARNESS.md)):
- Agent loop = flat single-turn, ≤6 grammar-constrained tool calls, no planning/reflection,
  no cross-turn goals, no context budgeting (fixed 8-row history, roles mislabeled).
- TaskScheduler runs **bare completions, never tools** — queued `generate_lab_report`
  produces no docx; `daily_summary` never calls the real Summarizer. `taskFinished` is
  connected to nothing → background results are never delivered.
- MemoryService (real hnswlib semantic store) is **unreachable from tools** (`ToolContext`
  lacks it); recall tools are keyword-LIKE fallbacks; nothing injects memory into prompts.
- 16 real leaf tools incl. a hand-rolled CDP `browser_drive` (web-only, throwaway sessions).

**Audio** (details in [04_VOICE_RESOURCES.md](04_VOICE_RESOURCES.md)):
- openWakeWord 3-model ONNX chain runs **continuously ungated** (~1–3 % CPU always).
- whisper base.en sits **resident in VRAM (~150 MB) at idle**; tiny.en loaded eagerly even
  though ambient mode is off.
- ASR + TTS run **synchronously on the single audio pump thread** → 4 s capture ring
  overflows during long transcriptions/speech; no barge-in; per-utterance `piper.exe` spawn.
- ONNX exceptions permanently disable wake/VAD until restart.

**Inference**: solid tiered InferenceManager + honest VramBudget; no KV-cache quantization
exposed; Fast model defaults to 8k ctx (~5.35 GB — too tight here). **No model files are on
disk** — `scripts/fetch-models.ps1` fetches the Gemma set (`docs/MODELS.md` is stale/Qwen-era).

**GUI**: centralized Style singleton + 8 `Pm*` controls + 10 views; zero effects/gradients;
inventory of drift + preserved-behavior list captured in [01_GUI_DESIGN_SYSTEM.md](01_GUI_DESIGN_SYSTEM.md).

**Repo**: git repo (branch `master`); `hearth/` at the workspace root is a **symlink** to
`polymath/` (reverted-rebrand leftover) — there is only one tree.

## 5. Target architecture (one paragraph per plane)

- **Body (C++/Qt services, unchanged skeleton)**: AudioService, VisionService,
  InferenceManager, Gateway (parked), Database/EventBus — reworked internally per specs but
  same service-on-QThread + EventBus pattern.
- **Mind (harness v2)**: `AgentRuntime` gains a goal/plan/reflect loop with SQLite-persisted
  goals + plan steps; TaskScheduler executes the *same* loop in the background; MemoryService
  and token-budgeted context assembly wired into every prompt; **skills** = declarative JSON
  bundles the model can invoke/author; delivery = every finished goal → notification + chat
  + optional TTS. Spec: [03_HARNESS.md](03_HARNESS.md).
- **Hands (executors)**: local leaf tools (existing 16, improved browser_drive) **+
  AgentSessionService** — provider adapters (ClaudeCode, Codex, generic PTY) spawning
  headless CLI sessions, parsed into a SessionsModel with needs-input notifications and
  spawn/send/status/stop tools. Spec: [05_AGENT_SESSIONS.md](05_AGENT_SESSIONS.md).
- **Face (GUI)**: holographic-glass design system + shell + feature views + SurfaceHost.
  Specs: [01_GUI_DESIGN_SYSTEM.md](01_GUI_DESIGN_SYSTEM.md), [02_GUI_FEATURES.md](02_GUI_FEATURES.md).
- **Voice**: VAD-gated wake word, on-demand GPU whisper, async pipeline with barge-in v1,
  streaming TTS. Spec: [04_VOICE_RESOURCES.md](04_VOICE_RESOURCES.md).

## 6. Execution

The whole overhaul is a **DAG of ~20 nodes in 4 waves**, designed for parallel subagents
with **disjoint file ownership** (this repo is the merge mechanism — two agents must never
edit the same file in the same wave). The DAG, node specs, acceptance criteria, and
verification loops: [06_DAG.md](06_DAG.md). The executable workflow script:
`.claude/workflows/polymath-overhaul.js`. Driver instructions (fresh session, any machine):
[07_KICKOFF.md](07_KICKOFF.md). Progress ledger: [PROGRESS.md](PROGRESS.md).

Verification backbone (cheap, no GPU): rebuild `pm_ui` + `capture_views` in `build/cpu`
(VS generator) → `capture_views.exe <out> [--empty]` renders every view to PNG offscreen →
read the PNGs. Full builds + live runs only at wave boundaries (see 06_DAG).

## 7. Document map

| Doc | Contents |
|---|---|
| [01_GUI_DESIGN_SYSTEM.md](01_GUI_DESIGN_SYSTEM.md) | Tokens, glass recipes, component library, shell, per-view checklists, motion |
| [02_GUI_FEATURES.md](02_GUI_FEATURES.md) | SettingsController, command palette, notifications, dashboard HUD plumbing, SurfaceHost |
| [03_HARNESS.md](03_HARNESS.md) | Goal/plan loop, memory wiring, context budgeting, skills, deep-task fix, permissions |
| [04_VOICE_RESOURCES.md](04_VOICE_RESOURCES.md) | VRAM/CPU budget, model set, audio pipeline rework, idle targets |
| [05_AGENT_SESSIONS.md](05_AGENT_SESSIONS.md) | AgentSessionService, provider adapters, SessionsModel, tools, GUI view |
| [06_DAG.md](06_DAG.md) | The master DAG: nodes, deps, file ownership, acceptance, verification |
| [07_KICKOFF.md](07_KICKOFF.md) | How a fresh driver session executes/resumes this plan |
| [PROGRESS.md](PROGRESS.md) | Living checklist — update + commit as nodes land |
