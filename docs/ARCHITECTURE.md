# Architecture

Single process (`Hearth.exe`), two halves wired by Qt signals/slots over a
thread-safe **EventBus**:

- **Frontend** — Qt Quick / QML views (Dashboard, Chat, Cameras, Tasks, Timeline,
  Shopping, Personalities, Models, Privacy). Backed by `AppController`
  (`src/app/`), exposed to QML as the context property `app`.
- **Backend services**, each a `QObject` on its own `QThread`, talking only via
  the EventBus — never direct cross-thread calls:

| Service | Module | Responsibility |
|---------|--------|----------------|
| InferenceManager | `src/inference` | Tiered llama.cpp models (Fast/Heavy/Vision/Embedding) + VRAM budget |
| TaskScheduler / Proactive / Idle | `src/scheduler` | Deep-work queue, reminders, idle-driven heavy load |
| AudioService | `src/audio` | capture → wake word → VAD → ASR → TTS |
| VisionService | `src/vision` | per-camera decode → motion → person → face → object-find |
| MemoryService | `src/memory` | SQLite + hnswlib vector index + daily summarizer |
| AgentRuntime | `src/agent` | tool registry + grammar-constrained tool-calling loop |
| PersonalityManager | `src/personality` | hot-loadable persona bundles |

## The two shared contracts
1. **EventBus message catalog** — `src/core/event_bus.h`. Every inter-service
   message is a signal here with a value-type payload (registered for queued
   delivery in `event_bus.cpp`). Add new cross-service messages here only.
2. **SQLite schema** — `src/core/schema.h` (`kSchemaSQL`). The single source of
   truth for persisted state; applied idempotently by `Database::migrate()`.

Plus two interfaces that decouple layers:
- `IModelBackend` (`i_model_backend.h`) — one loaded model; implemented by the
  llama.cpp backend.
- `ITool` (`i_tool.h`) — one agent capability; implemented under `agent/tools/`.

## Tiered inference (the key idea)
A small **Fast** model stays resident for real-time voice/chat. The
**TaskScheduler** queues heavy jobs (lab reports, research, daily summaries);
when the **IdleDetector** reports the machine quiet, it asks the
**InferenceManager** to load the **Heavy** model within the ~8 GB VRAM budget
(evicting/▾offloading the resident set as needed), drains the queue, then
restores Fast. **Vision** (VLM) and **Embedding** models load on demand too.

## Threading rules
- Construct services in dependency order in `AppController::initialize()`, then
  `runOnThread()` each (`src/core/service.h`).
- UI → backend: `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` to a slot,
  or publish on the EventBus.
- Backend → UI: emit an EventBus signal; `AppController` re-emits Qt signals the
  QML binds to.
