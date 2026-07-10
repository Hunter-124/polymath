# Architecture

Single process (`Polymath.exe`), two halves wired by Qt signals/slots over a
thread-safe **EventBus**:

- **Frontend** — Qt Quick / QML: frameless holographic shell (`Main.qml`), feature
  views (Dashboard, Chat, Cameras, Tasks, Timeline, Shopping, Personalities, Models,
  Privacy, Mobile, Settings, Agent Sessions), plus overlays (CommandPalette,
  ToastStack, NotificationBell/Center, SurfaceHost). Backed by `AppController`
  (`src/app/`), exposed to QML as the context property `app`. Settings live on a
  dedicated `SettingsController` (`settings`); list models are separate context
  properties (`chatModel`, `agentSessions`, `notifications`, …).
- **Backend services**, each a `QObject` on its own `QThread`, talking **only** via
  the EventBus — never direct cross-thread QObject calls from tools:

| Service | Module | Responsibility |
|---------|--------|----------------|
| InferenceManager | `src/inference` | Tiered llama.cpp models (Fast/Heavy/Vision/Embedding) + VRAM budget |
| TaskScheduler / Proactive / Idle | `src/scheduler` | Deep-work queue, reminders, idle-driven load; same tool loop as interactive |
| AudioService | `src/audio` | capture → VAD-gated wake → ASR → TTS (async workers) |
| VisionService | `src/vision` | per-camera decode → motion → person → face → object-find |
| MemoryService | `src/memory` | SQLite + hnswlib vector index + daily summarizer |
| AgentRuntime + AgentLoop | `src/agent` | harness v2: router, goals, skills, tool registry |
| AgentSessionService | `src/sessions` | external CLI agents (Claude Code / Codex / PTY) |
| PersonalityManager | `src/personality` | hot-loadable persona bundles |

## Planes (overhaul target)

| Plane | Role |
|-------|------|
| **Body** | C++/Qt services above — same skeleton, reworked internals |
| **Mind** | AgentLoop v2: plan → execute → reflect; skills; memory-injected prompts |
| **Hands** | Local leaf tools + AgentSessionService adapters |
| **Face** | Holographic glass GUI + SurfaceHost content composition |
| **Voice** | VAD-gated wake, on-demand whisper, async ASR/TTS, barge-in |

Detailed specs: [`docs/overhaul/`](overhaul/) (`00_MASTER_PLAN.md` first).

## The two shared contracts

1. **EventBus message catalog** — `src/core/event_bus.h`. Every inter-service
   message is a signal here with a value-type payload (registered for queued
   delivery in `event_bus.cpp`). Add new cross-service messages here only.
2. **SQLite schema** — `src/core/schema.h` (`kSchemaSQL`). Applied idempotently by
   `Database::migrate()`. Includes goals/plan_steps, agent_sessions, conversation
   summaries, privacy/retention tables, etc.

Plus interfaces that decouple layers:

- `IModelBackend` (`i_model_backend.h`) — one loaded model; llama.cpp implementation.
- `ITool` (`i_tool.h`) — one agent capability; `ToolContext` carries `Database*`,
  `InferenceManager*`, `MemoryService*`, user/personality.
- `IAgentProvider` (`src/sessions/i_agent_provider.h`) — one external CLI product.

---

## EventBus payloads (summary)

Classic payloads (audio, inference, vision, scheduler, privacy) plus overhaul
additions:

| Payload | Purpose |
|---------|---------|
| `WakeWordEvent` / `Utterance` / `SpeakRequest` | Voice in/out |
| `TokenChunk` | Streaming LLM tokens |
| `ToolCallEvent` / `ToolResultEvent` | Tool telemetry for UI |
| `Frame` / `Detection` / `FindObjectResult` | Vision tiles + results |
| `TaskEvent` / `ReminderFired` | Scheduler / proactive |
| `PrivacyChanged` / `Notice` | Privacy toggles + log/toast surface |
| **`SurfaceRequest`** | spawn / close / arrange / open_page content surfaces |
| **`GoalUpdate`** | goal terminal/progress → notifications + chat delivery |
| **`AgentSessionEvent`** | normalized external-agent events (Started, NeedsInput, Result, …) |

Publish helpers: `EventBus::publishSurfaceRequest`, `publishGoalUpdate`,
`publishAgentSessionEvent`, etc. `AppController` relays UI-facing signals that
QML binds to.

---

## Harness v2 (`AgentLoop`)

Source: `src/agent/agent_loop.*`. `AgentRuntime` owns the loop and tool registry;
**the same loop object** runs interactive turns and background scheduler jobs.

### Turn router (interactive)

Fast-model (or heuristic fallback) classification:

| Route | Behavior |
|-------|----------|
| `quick` | Answer / ≤ few leaf tools — snappy v1 path |
| `goal` | Multi-step: create goal + plan |
| `command` | Pure UI/skill (e.g. surface spawn) without a long plan |

### Goal lifecycle: plan → execute → reflect

1. **Plan** — constrained JSON `{title, steps[]}` (max 12); skills expand to
   pre-authored steps. Persisted to `goals` / `plan_steps` before execution.
2. **Execute** — sequential steps by `kind`:
   - `tool` → `ITool::invoke`
   - `prompt` → completion with accumulated context
   - `skill` → expand inline
   - `agent_session` → park goal `waiting_agent`; resume on session events
   - `surface` → `SurfaceRequest` on the bus
3. **Reflect** — on failure (attempts &lt; 3): revise remaining plan; else mark goal
   `failed`. Wall-clock cap (`agent.goal_timeout_min`), total-step cap.

### Delivery

On terminal goal state: publish **`GoalUpdate`** → NotificationsModel + assistant
chat line + optional TTS (`agent.speak_results`). `TaskScheduler::taskFinished`
uses the same delivery path (no more black-hole results).

### Context assembly v2

Token-budgeted for **n_ctx 4096** (via `InferenceManager::countTokens`):

| Bucket | Budget (tokens) |
|--------|-----------------|
| system + persona + tool catalog | ≤ 1100 |
| semantic memories (top-k recall) | ≤ 400 |
| rolling conversation summary | ≤ 400 |
| verbatim recent turns (correct roles) | ≤ 1400 |
| reserve (generation + tool results) | ≥ 700 |

Memory tools use the real semantic path when the embedder is available; keyword
fallback otherwise. Transcript roles are labeled correctly (User/Assistant).

### Skills

Declarative workflow bundles at `data/skills/<name>/skill.json` (hot-reloaded):

- `SkillRegistry` validates, substitutes `{params}`, expands to plan steps.
- Tools: `run_skill`, `save_skill` (AI-authored skills force `confirm: true`).
- Starters: `slop_mode`, `morning_brief`, `research_brief`.

### Tool risk classes

Registered in `register_tools.cpp` (~**25** tools):

| Class | Policy sketch |
|-------|----------------|
| Read | Auto |
| WriteLocal | Auto, local side effects |
| External | Auto, logged + notice (network / CLI) |
| Spend | Confirm / park `waiting_user` (print, …) |

Includes `ui_control` (surfaces), `browser_drive` (persistent Chrome session +
screenshot), and the full `agent_*` set.

---

## Agent sessions ("transparent cowork")

Module: `src/sessions/`. Service on its own QThread.

- **Providers** implement `IAgentProvider`: spawn / send / stop + normalized
  `AgentEvent` stream → `AgentSessionEvent` on the bus.
  - `ClaudeCodeProvider` — `claude -p … --output-format stream-json`
  - `CodexProvider` — experimental badge
  - `GenericPtyProvider` — config-driven catch-all
- **Persistence** — `agent_sessions` table (id, provider, title, cwd, status,
  cost, last message, …).
- **Gates** — cwd allowlist, max concurrent sessions; **never auto-approve**
  CLI permission prompts (relay as NeedsInput).
- **UI** — `SessionsModel` + `AgentSessionsView`; toast/voice on needs_input when
  configured (`agents.speak_needs_input`).

---

## Surfaces (AI-composed screen)

- EventBus **`SurfaceRequest`**: `action` spawn|close|arrange|open_page;
  `type` placeholder|image|web|video|monitor.
- QML **`SurfaceHost`** + `surfaces/*` (`PlaceholderSurface`, `ImageSurface`,
  `WebSurface`).
- **`ui_control` tool** publishes requests; demo path `spawnSurfaceDemo` on
  AppController.
- **QtWebEngine is optional** (DAG D5). Without it, web type is a graceful
  placeholder (kit Qt 6.6.3 without WebEngine module).

---

## Audio pipeline (summary)

`AudioService` (`src/audio/*`) — A4 rework:

```
WASAPI capture → Silero VAD (always-on gate)
              → openWakeWord only during VAD speech (+ hangover)
              → segment bookkeeping on pump thread
              → AsrWorker QThread (whisper_full, GPU on-demand)
              → EventBus utterance → agent
              → TtsWorker QThread (persistent Piper, sentence streaming)
```

- Whisper **not** idle-resident; load on wake/PTT, unload after
  `audio.asr_idle_unload_s` (default 90 s).
- Capture ring **16 s**; barge-in v1 cancels TTS on new speech.
- Privacy: mic off stops capture; ambient ASR only if enabled.

Resource budget: [`docs/overhaul/04_VOICE_RESOURCES.md`](overhaul/04_VOICE_RESOURCES.md).

---

## Tiered inference

A small **Fast** model stays resident for real-time voice/chat (**default
n_ctx=4096**, **KV quant q8_0** so an 8 GB Max-Q has browser/video headroom).

**TaskScheduler** queues deep jobs; when **IdleDetector** reports quiet,
**InferenceManager** may load heavier roles within **VramBudget**, drain the
queue, restore Fast. **Vision** (VLM) and **Embedding** load on demand.
**Heavy 27B is parked** on this hardware — deep reasoning prefers agent-session
delegation or Fast idle work.

---

## Threading rules

- Construct services in dependency order in `AppController::initialize()`, then
  `runOnThread()` each (`src/core/service.h`).
- UI → backend: `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` or EventBus.
- Backend → UI: EventBus signal; `AppController` re-emits properties/signals QML binds.
- Tools never touch QObjects on other threads directly — only value payloads on the bus
  (or documented invokables on the sessions service thread).

---

## GUI design system (pointer)

Tokens and glass recipes: [`docs/overhaul/01_GUI_DESIGN_SYSTEM.md`](overhaul/01_GUI_DESIGN_SYSTEM.md).
Feature plumbing (settings, palette, notifications, SurfaceHost):
[`docs/overhaul/02_GUI_FEATURES.md`](overhaul/02_GUI_FEATURES.md).

Rule of thumb: **Style.qml singleton only** — no hardcoded colors/sizes in views.
Any new QML↔C++ surface must be mirrored in `src/ui/tools/capture_views.cpp` stubs.
