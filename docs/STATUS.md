# Build status

Snapshot after the Wave-0 scaffold + the fanned-out module batch (Waves 1–3).

> **Honest caveat:** none of this has been **compiled** yet — the dev environment
> has no Qt/CUDA/vcpkg toolchain. The C++ is written against the pinned library
> APIs below and is internally consistent (every `CMakeLists` source list matches
> disk; the frozen `core` contracts — EventBus, schema, `IModelBackend`, `ITool`
> — were respected by every module). Treat each module as **implemented but
> needs on-machine compile verification**. First real compile will surface API
> drift to fix (especially llama.cpp / ONNX Runtime, which move fast).

## Modules

| Module | Status | Key files | ~LOC |
|--------|--------|-----------|------|
| `core` | ✅ hand-built, stable contracts | event_bus, database, schema, config, paths, i_model_backend, i_tool | — |
| `inference` | 🟡 implemented, unverified | llama_backend, vram_budget, grammar, inference_manager | 1160 |
| `audio` | 🟡 implemented, unverified | capture, wakeword, vad, asr_whisper, tts_piper, onnx_util | 1111 |
| `vision` | 🟡 implemented, unverified | camera_worker, motion, detector_yolo, face_arcface, visual_memory, finder | 1433 |
| `scheduler` | 🟡 implemented, unverified | task_scheduler, proactive_engine, idle_detector, scheduler_util | 622 |
| `memory` | 🟡 implemented, unverified | vector_index (hnswlib), summarizer, memory_service | 949 |
| `agent` | 🟡 implemented, unverified | agent_runtime, persona, turn_collector, tools/* (web, fetch, docs, docx, print, shopping, reminders, memory, camera, queue) | 2344 |
| `personality` | 🟡 implemented, unverified | personality_manager, bundle_seed | 450 |
| `ui` + `app` | 🟡 implemented, unverified | models/{chat,shopping,camera,task,timeline}, camera_image_provider, AppController, QML views | 676 |
| `firmware/esp32cam` | ✅ complete | esp32cam.ino, config.h | — |

Total: **~13,000 lines of C++** plus the QML UI and ESP32 firmware.

## Assumed native library versions (pin these)
- **llama.cpp** — recent master (uses `llama_*` API + `mtmd`/clip for VLM). The
  sampler/`llama_decode` and `mtmd` APIs change often; expect the most fixups here.
- **whisper.cpp** — recent master (`whisper_full`).
- **ONNX Runtime (GPU)** 1.17+ — C++ `Ort::` API (wakeword, VAD, YOLO, face).
- **OpenCV** 4.x (vcpkg, with `ffmpeg` + `dnn`).
- **Piper** — `libpiper` / phonemize + onnxruntime voices.
- **hnswlib**, **miniaudio** — header-only, vendored.

## Removed (out-of-scope drift from a runaway agent)
One agent generated an unrequested **remote-access stack** — a React/Capacitor
mobile app (`app/`), a TypeScript **cloud relay** (`cloud/relay/`), and a C++
`src/gateway/`. These were **removed**: they weren't in the approved plan, weren't
wired into the build, and a *cloud relay contradicts the local-first / no-telemetry*
privacy posture. A LAN-only companion app could be added later as a properly
scoped feature if desired.

## Known follow-ups (first build pass)
1. **Compile** with the real toolchain (`scripts/setup-dev.ps1` → `cmake --build`)
   and fix native-API drift module by module (inference first).
2. A few agent tools intentionally use **SQL fallbacks** where the frozen
   `ToolContext` only exposes `db`+`inference` (e.g. `recall` does SQL LIKE; full
   vector recall lives in MemoryService). Wire these via the EventBus if richer
   coupling is wanted.
3. **Model files** are not included — run `scripts/fetch-models.ps1` and assign
   roles in the Model Manager.
4. Verify end-to-end against [PLAN.md §16](PLAN.md) (voice loop, vision, deep
   task, proactive, privacy toggles).

## What works without a GPU
The `core` layer (DB/schema/config/eventbus) and the reference tools compile as
plain C++ — `tests/test_core` and `tests/test_tools` exercise them and don't need
any model or native engine.
