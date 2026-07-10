# Plan: Local AI Home Assistant (codename **Polymath**)

## Context

We are building, from scratch (the working directory is empty), an always‑on, fully‑local AI
home assistant for Windows. It is an ambient agent that listens for a wake word, transcribes
speech, thinks with a **local llama.cpp model**, speaks back, and can *think as different
historical personalities* that are modular and swappable. Beyond conversation it perceives the
home through ESP32‑CAM video (motion + person/face detection, object finding), keeps long‑term
memory of the household, proactively suggests/reminds (vitamins, dinner plans, next‑day tasks),
and wields a rich agent toolset (web search, image analysis, document/lab‑report drafting,
printing, shopping lists, browser/internet access).

Key product decisions already made with the user:

- **Stack:** C++ with **Qt 6 / Qt Quick (QML)** UI. One compiled application (see *Packaging* for
  the realistic meaning of "single binary").
- **Hardware:** ~8 GB NVIDIA GPUs, CUDA. **Tiered inference**: a small *fast* model stays
  resident for real‑time voice; a user‑supplied *heavy* model is loaded on demand to drain a
  queue of long/important "deep‑work" tasks when the machine is idle, then unloaded.
- **Scope:** ESP32 + "life‑organizer" basics (no third‑party smart‑home hub). Ambient daily
  intelligence is a first‑class feature.
- **Web:** phased — local search + page fetch first, real browser automation later.
- **Cameras:** we also ship **ESP32‑CAM firmware** + flashing docs (user hasn't set them up).
- **Privacy:** *configurable, default ON* — full ambient transcription + face recognition, with
  a master privacy panel, per‑feature toggles, retention controls, and at‑rest encryption. All
  data stays local.
- **Build order:** full architecture scaffolded up front, then executed as a fanned‑out
  multi‑agent batch (the user will `/batch` after approving this plan).

> Naming: "Polymath" is a working codename (fits *wide learning / many historical minds*); easily
> changed in one config constant.

---

## 1. High‑level architecture

Single process, two halves wired by Qt signals/slots over a thread‑safe **event bus**:

```
┌───────────────────────────────  Polymath.exe  ───────────────────────────────┐
│  Qt Quick / QML Frontend  (UI thread)                                       │
│   Dashboard │ Cameras │ Chat │ Task Queue │ Memory/Timeline │ Shopping │     │
│   Personalities │ Model Manager │ Privacy/Settings                          │
│        ▲  Q_PROPERTY / QAbstractListModel / signals                         │
│        │                                                                    │
│   AppController (QObject facade)  ◄──────────────  EventBus  ──────────────┐ │
│        │                                                                  │ │
│  Backend services (worker threads):                                       │ │
│   • InferenceManager  (llama.cpp: fast / heavy / vision / embedding)       │ │
│   • TaskScheduler     (priority + deep-work queue, idle-driven loading)    │ │
│   • AudioService      (capture → wakeword → VAD → ASR → TTS)               │ │
│   • VisionService     (per-camera: decode → motion → person → face → find) │ │
│   • MemoryService     (SQLite + vector index, daily summarizer)            │ │
│   • AgentRuntime      (tool registry, grammar-constrained tool calling)    │ │
│   • PersonalityManager(hot-loadable persona bundles)                       │ │
│   • ProactiveEngine   (reminders, schedules, next-day suggestions)         │◄┘
└─────────────────────────────────────────────────────────────────────────────┘
        SQLite (spine)   +   on-disk: models/, personalities/, media/, vectors
```

Design rules so the batch can build modules in parallel without colliding:

- Every service is a `QObject` living on its own `QThread`; cross‑service calls go through the
  **EventBus** (typed signals) — never direct method calls across threads.
- The **SQLite schema** (§7) and the **EventBus message catalog** are the two shared contracts.
  They are defined in this plan and owned by a single "core" module; service authors code against
  them, not against each other.
- The **ITool** and **IModelBackend** interfaces (§2, §8 sketches) decouple the agent and
  inference layers from concrete implementations.

---

## 2. Inference Engine Manager (tiered models)

Wraps **libllama** (llama.cpp) directly. Owns all GPU memory budgeting.

- **Roles:** `Fast` (resident, real‑time chat/voice), `Heavy` (on‑demand, deep tasks),
  `Vision` (VLM, on‑demand or shared), `Embedding` (memory/RAG).
- **VRAM arbitration:** an `~8 GB` budget manager. Loading Heavy evicts Fast/Vision (or reduces
  `n_gpu_layers` / offloads to CPU), runs the queued deep task(s), then restores the resident set.
  Heavy may run partially on CPU (slow but allowed — this is the "sit back and think hard" path).
- **Streaming** token callbacks → EventBus → UI / TTS.
- **Tool calling:** GBNF grammar‑constrained JSON decoding so even small local models emit valid
  tool calls; also supports models with native tool templates (Qwen/Llama).
- **Model registry** (GUI‑editable, persisted): path, role, quant, `n_ctx`, `n_gpu_layers`,
  chat template, mmproj (for VLM). Users add custom GGUFs and assign roles.

```cpp
struct IModelBackend {            // implemented by LlamaBackend
  virtual void load(const ModelSpec&) = 0;
  virtual void unload() = 0;
  virtual void generate(const ChatRequest&, TokenCallback) = 0; // streaming
  virtual std::vector<float> embed(std::string_view) = 0;
  virtual std::string describeImage(const Frame&, std::string_view prompt) = 0; // VLM
};
```

Default model set (all swappable in the Model Manager):

| Role | Default | Notes |
|------|---------|-------|
| Fast LLM | Qwen2.5‑7B‑Instruct Q4_K_M | strong tool‑calling, fits 8 GB w/ context |
| Heavy LLM | user‑supplied (e.g. QwQ‑32B / Qwen2.5‑32B/72B Q4) | partial offload, queued only |
| Vision VLM | Qwen2.5‑VL‑7B or MiniCPM‑V 2.6 (GGUF + mmproj) | image analysis, "find my keys" |
| Embedding | nomic‑embed‑text‑v1.5 (GGUF) | memory/RAG |

---

## 3. Task Scheduler (deep‑work queue)

- Priority queue persisted in SQLite (`tasks` table). Real‑time voice bypasses the queue.
- **Deep tasks:** lab‑report generation, multi‑step research, document drafting, end‑of‑day
  summarization, batch image analysis. Each is a serialized job (type + params + status + result).
- **Idle detector:** no active conversation + mic/scene quiet ⇒ ask InferenceManager to load Heavy,
  drain the queue, release VRAM. Configurable quiet hours.
- Emits progress/results to UI (Task Queue view) and can deliver results by TTS/notification.

---

## 4. Audio Service

Pipeline (own thread, low‑latency ring buffer):

`WASAPI capture → wake‑word → VAD → ASR → (AgentRuntime) → TTS`

- **Capture:** miniaudio (vendored, header‑only) for robust WASAPI; 16 kHz mono.
- **Wake word:** **openWakeWord** (ONNX via ONNX Runtime). Default "Hey Jarvis"; supports custom
  trained words and per‑personality wake phrases. Always‑on, tiny CPU.
- **VAD:** **Silero VAD** (ONNX) to segment speech and gate ASR (saves GPU during silence).
- **ASR:** **whisper.cpp** (libwhisper, CUDA). Two configs: *command* (small/medium, accurate,
  post‑wakeword) and *ambient* (tiny/base, continuous, privacy‑gated) for daily transcription.
- **TTS:** **Piper** (vendored, ONNX). Each personality maps to a Piper voice; streamed playback.
- **Diarization (optional/later):** small ONNX speaker‑embedding to label "who said what" for the
  ambient log. Flagged behind privacy toggle.

EventBus out: `WakeWordDetected`, `Utterance{text,speaker?,ts}`, `AmbientTranscript{...}`,
`SpeakRequest{text,voice}` in.

---

## 5. Vision Service

One worker per camera; shared model pool for detection.

- **Ingest:** OpenCV `VideoCapture` / FFmpeg for ESP32‑CAM **MJPEG/RTSP**. Auto‑reconnect.
- **Motion:** OpenCV MOG2 background subtraction (cheap, continuous) → gates the heavier stages.
- **Person detection:** **YOLO11n/YOLOv8n** (ONNX Runtime, CUDA) on motion.
- **Face recognition (privacy‑gated):** SCRFD detect + ArcFace embed (ONNX, InsightFace) matched
  against an **enrolled user gallery** ⇒ "who is home / track user".
- **Object finding ("find my keys"):** maintains a rolling **visual memory** (recent frames +
  detections w/ timestamps & camera). On request, runs the VLM (open‑vocabulary) over live + recent
  frames and answers "last seen on the kitchen counter at 14:03 (cam 2)".
- **Events:** motion/person/face events + thumbnails persisted (`events` table + `media/`).
- **Live tiles** streamed to QML camera grid with overlays.

EventBus: `MotionEvent`, `PersonEvent`, `FaceEvent{userId?}`, `FrameForUI`, `FindObjectResult`.

### ESP32‑CAM firmware (shipped)
`firmware/esp32cam/` — Arduino/ESP‑IDF sketch: Wi‑Fi config + MJPEG streaming server (AI‑Thinker
ESP32‑CAM pin map), `/stream` + `/snapshot` + `/config` endpoints, mDNS name. Includes flashing
guide (Arduino IDE / `esptool`) and a board‑selection note.

---

## 6. Memory & Knowledge + Proactive Engine

- **SQLite spine** (§7) for everything structured.
- **Vector index:** **hnswlib** (vendored) or `sqlite-vec` for semantic recall over transcripts,
  notes, documents, and event captions.
- **Daily pipeline** (a deep task): summarize the day's ambient transcript + events ⇒ extract
  action items, generate *"suggestions for tomorrow"*, dinner‑plan prompts, and reminders.
- **ProactiveEngine:** time/condition‑based reminders (vitamins, dinner prep, calendar) delivered
  via TTS + UI notification; respects quiet hours and presence (only nag when someone's home).

---

## 7. Data model (SQLite, encryptable via SQLCipher)

Core tables (owned by the `core` module; all services code against these):
`models`, `personalities`, `tasks`, `reminders`, `shopping_items`, `events`(vision/audio),
`transcripts`(ambient + command, with `speaker`, `is_ambient`, retention ttl), `users`(face/voice
enrollment, gallery refs), `memories`(text + vector id), `documents`(drafts/reports), `settings`
(incl. privacy toggles + retention), `cameras`. Media blobs (thumbnails, voices, frames) live under
`media/` referenced by path. Retention sweeper enforces per‑category TTL.

---

## 8. Personality system (modular)

A personality is a **hot‑loadable data bundle**, not code: `personalities/<name>/persona.json`
+ optional `avatar.png` + voice ref. Drop a folder in to add one.

```json
{
  "name": "Marcus Aurelius",
  "system_prompt": "You are Marcus Aurelius, Roman emperor and Stoic ...",
  "voice": "en_GB-alan-medium",          // Piper voice id
  "sampling": { "temperature": 0.7, "top_p": 0.9 },
  "preferred_model": "fast",              // or a specific registry id
  "wake_phrase": "Marcus",               // optional per-persona wake word
  "tools": ["web_search","draft_document","image_analyze"]  // allow-list
}
```

GUI **Personality Manager**: add/edit/duplicate/switch; live switch changes system prompt + TTS
voice + (optionally) model. Ships with a starter set of historical figures.

---

## 9. Agent runtime & tools

LLM↔tools via a registry; the runtime builds the tool list per turn (filtered by the active
personality's allow‑list), constrains output to valid JSON (GBNF), executes, feeds results back,
loops until the model answers.

```cpp
struct ITool {
  std::string name; ToolSchema schema;            // JSON-schema params
  virtual ToolResult invoke(const json& args, ToolContext&) = 0;
};
```

Tools (Phase‑tagged):

- **web_search** + **fetch_page** (readability extract) — *now*. Backend: SearXNG/Brave/DDG
  (configurable key).
- **browser_drive** — *later*: Chrome DevTools Protocol (websocket from C++) for clicking, forms,
  logged‑in sites; can attach to the user's Chrome or a bundled headless Chromium.
- **image_analyze / find_object** → VisionService + VLM.
- **draft_document / generate_lab_report** → templated DOCX/PDF generation (direct OOXML write or
  vendored lib); saved to `documents/`, openable/printable.
- **print_document / print_image** → `QPrinter` (Windows print subsystem).
- **shopping_add / shopping_list / shopping_remove** → SQLite.
- **set_reminder / schedule_task** → ProactiveEngine/Scheduler.
- **remember / recall / search_memory** → MemoryService (vector + SQL).
- **camera_snapshot / who_is_home** → VisionService.
- **queue_deep_task** → push a heavy job (research/report) to the Scheduler.

---

## 10. GUI (Qt Quick / QML)

Views, each backed by a C++ context object / `QAbstractListModel`:

- **Dashboard** — listening state, active personality, loaded model + VRAM, today's reminders &
  suggestions, quick actions.
- **Cameras** — live grid, motion/person overlays, snapshot, "find object" search box.
- **Chat** — voice+text transcript, push‑to‑talk, streaming replies, personality selector.
- **Task Queue** — pending/running deep tasks + results (open drafted docs/reports).
- **Memory / Timeline** — daily summaries, searchable transcripts & events.
- **Shopping List**.
- **Personality Manager** (§8).
- **Model Manager** (§2) — add GGUF, assign roles, GPU layers, quick test.
- **Privacy / Settings** — master + per‑feature toggles (mic, ambient transcription, face ID,
  cameras), retention sliders, at‑rest encryption, quiet hours.

---

## 11. Repository layout

```
/CMakeLists.txt            vcpkg.json            /docs (setup, ESP32 flashing, privacy)
/third_party/  llama.cpp  whisper.cpp  piper  openWakeWord(onnx)  silero  hnswlib  miniaudio
/src/
  core/        eventbus, db (sqlite), config, logging, schema, types        ← shared contract
  inference/   llama_backend, model_manager, vram_budget, grammar
  scheduler/   task_queue, idle_detector, proactive_engine
  audio/       capture, wakeword, vad, asr_whisper, tts_piper
  vision/      camera_worker, motion, detector_yolo, face_arcface, visual_memory, finder
  memory/      store, vector_index, summarizer
  agent/       runtime, tool_registry, tools/*  (web, docs, print, shopping, home, memory)
  personality/ manager, bundle_loader
  app/         app_controller (QObject facade), main.cpp
  ui/          qml/* , view-model C++ glue
/firmware/esp32cam/   sketch + README
/assets/personalities/   starter historical personas
```

---

## 12. Build & packaging — the honest "single binary"

- **CMake + vcpkg** for Qt6, OpenCV, ONNX Runtime, libcurl, SQLite/SQLCipher, FFmpeg.
- llama.cpp / whisper.cpp / piper as `third_party` submodules built **with CUDA**.
- We statically link our own code and as many libs as practical (static Qt build → one app `.exe`).
- **Unavoidably separate at runtime** (be explicit with the user): NVIDIA CUDA runtime DLLs
  (`cudart`, `cublas`, `cublasLt`), `onnxruntime.dll`, and **model/data files** (GGUF, ONNX, Piper
  voices) which are loaded from `models/` — these are *data*, not code, and cannot live inside the
  `.exe`.
- **Deliverable:** one primary `Polymath.exe` + a small set of runtime DLLs + `models/`,
  `personalities/`, `firmware/` folders, wrapped as a **portable zip** and/or an installer. That is
  the practical maximum of "one compiled binary" for a CUDA + Qt Quick app; documented in `/docs`.

---

## 13. Privacy & security (default ON, configurable)

- All inference and storage local; **no telemetry**. Optional **SQLCipher** at‑rest encryption.
- Master kill‑switch + per‑feature toggles (mic, ambient transcription, face ID, each camera).
- Retention TTL per data class (ambient transcripts shortest by default) with a sweeper.
- Face/voice enrollment is explicit and opt‑in per person; galleries deletable.
- Web/browser tools are allow‑listed per personality and surfaced in the activity log.

---

## 14. Execution plan (the `/batch` after approval)

Build via a fanned‑out multi‑agent workflow. **Wave 0 (serial, foundation)** first so the shared
contracts exist before parallel work:

- **Wave 0 — Core scaffold:** CMake + vcpkg + third_party submodules; `core/` (EventBus, SQLite
  schema §7, config, types, `IModelBackend`/`ITool` interfaces); empty `main.cpp` + QML shell that
  launches. *This defines the contracts every other agent codes against.*

Then **Waves 1–3 in parallel** (one agent per module, each against Wave‑0 contracts, isolated
worktrees to avoid file collisions):

- **Wave 1:** `inference` (Llama backend + model manager + VRAM budget + grammar) ·
  `audio` (capture/wakeword/vad/whisper/piper) · `vision` (camera/motion/yolo) ·
  `firmware/esp32cam`.
- **Wave 2:** `scheduler`+`proactive` · `memory`+vector+summarizer · `agent` runtime + tool
  registry + Phase‑1 tools (web_search, fetch_page, shopping, reminders, docs, print, memory) ·
  `personality` manager · `vision` face+finder.
- **Wave 3:** `ui` QML views + `app_controller` glue wiring all services to the GUI; integration
  pass; packaging (static link + windeployqt + zip/installer); browser‑automation tool (Phase 2).

Each agent delivers: headers + impl + a minimal unit/integration test + a short "how to run" note.
A final integration agent compiles, fixes link/threading issues, and produces a smoke‑test build.

---

## 15. Risks & mitigations

- **8 GB VRAM contention** (Fast + Whisper + YOLO + VLM simultaneously) → strict VRAM budget mgr,
  ambient ASR on CPU/tiny, VLM/Heavy on‑demand only, quantized everything.
- **C++ ecosystem gaps** (face rec, VAD, wakeword are Python‑native) → use **ONNX Runtime** with
  pre‑exported ONNX models; no Python dependency.
- **True single .exe impossible with CUDA/Qt Quick** → set expectation now (§12); ship portable
  bundle.
- **Always‑on listening privacy** → local‑only, encryption, retention, explicit toggles (§13).
- **Scope** is very large → Wave‑0 contracts + isolated worktrees let the batch parallelize safely;
  Phase‑2 items (browser automation, diarization) explicitly deferred.

---

## 16. Verification (end‑to‑end)

1. **Build:** `cmake --preset cuda-release && cmake --build` → produces `Polymath.exe`; launches to
   the QML Dashboard.
2. **Inference:** Model Manager loads the Fast GGUF; a text chat returns a streamed reply; a tool
   call (e.g. `shopping_add "milk"`) round‑trips and the item appears in the Shopping view.
3. **Voice:** speak the wake word → ASR transcribes → reply spoken via Piper; switch personality →
   voice + persona change audibly.
4. **Vision:** point an ESP32‑CAM (flashed from `firmware/`) at the app → live tile; walk through →
   motion + person event; enroll a face → `who_is_home` identifies; "find my keys" returns a
   last‑seen answer.
5. **Tiered/deep task:** queue a "generate lab report" deep task → Scheduler loads Heavy during
   idle, produces a DOCX/PDF in `documents/`, which prints via `print_document`.
6. **Ambient/proactive:** with ambient transcription on, leave it running; the daily summarizer
   produces next‑day suggestions and a reminder fires by TTS.
7. **Privacy:** toggle mic/face off in the Privacy panel → confirm capture stops and retention
   sweeper purges expired ambient transcripts.
8. **Tests:** `ctest` runs the per‑module unit/integration tests green.
```
