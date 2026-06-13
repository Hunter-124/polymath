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

## Device fabric (v0.2)

The fabric bridges autonomous edge devices (cameras, voice satellites, lab
instruments, panels) onto the hub's two frozen contracts — the EventBus and
the SQLite schema — **without changing either of them**.

| Module | Location | Role |
|---|---|---|
| `FabricService` | `src/fabric/fabric_service.h` | normalizes wire payloads → DB rows + EventBus signals |
| `DeviceRegistry` | `src/fabric/device_registry.h` | in-memory view of the `edge_devices` table; tracks online state |

Two transport paths both call the same `ingest*` methods:

* **HTTP plane** — `HttpRouter` routes `POST /fabric/devices/announce`,
  `POST /cameras/:id/events`, and `POST /cameras/:id/frame` directly to
  `FabricService::ingestAnnounce / ingestCameraEvent / ingestFrame`. Works
  with no broker.
* **MQTT plane** — when built with `POLYMATH_USE_MQTT`, an internal client
  subscribes to `hearth/#` and routes messages to the same `ingest*` methods
  (see `FabricService::MqttImpl`).

`FabricService` is the only component that knows about the fabric wire format.
After it normalizes a payload it:

1. Upserts rows in `edge_devices`, `instruments`, `measurements`, or `events`
   (including the new `clip_url / confidence / device_id` columns).
2. Publishes the matching EventBus signal (`publishInstrumentReading`,
   `publishDevicePresence`, `publishDetection`, `publishLabStep`, …).

Everything downstream — WsHub, mobile app, UI models, agent tools — already
listens on those signals and reads from those tables. Nothing downstream
required changes.

**`SpeakRequest.target`** (EventBus) and **`SpeakEvent.target`** (WS) carry an
optional voice-satellite id so the TTS router can send audio back to the room
the utterance came from via `cmd/tts`; empty string means the local speaker.

For the full wire protocol (MQTT topics, JSON shapes, SoftAP pairing, clip
contract, UDP audio plane) see **[`docs/FABRIC.md`](FABRIC.md)**.

## Threading rules
- Construct services in dependency order in `AppController::initialize()`, then
  `runOnThread()` each (`src/core/service.h`).
- UI → backend: `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` to a slot,
  or publish on the EventBus.
- Backend → UI: emit an EventBus signal; `AppController` re-emits Qt signals the
  QML binds to.
