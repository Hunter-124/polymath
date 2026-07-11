# Polymath — a fully-local AI home assistant

Polymath is an always-on, **100% local** AI home assistant for Windows: one C++/Qt Quick
application — no cloud, no telemetry, no account. It listens, sees, remembers, and acts,
entirely on your own machine and GPU. Vision: a Jarvis / Star Trek-computer presence —
voice-first companion, real multi-step goals, and transparent delegation to external AI
CLIs when the hard work needs a bigger brain.

## What it does

- **Voice loop** — VAD-gated wake word → speech-to-text (whisper.cpp, on-demand GPU) →
  local LLM (llama.cpp) → text-to-speech (Piper, streaming sentences). Push-to-talk or
  hands-free; barge-in supported.
- **Agent harness v2** — multi-step **goals** that plan → execute → reflect, persist across
  turns/restarts, and deliver results to chat + notifications (+ optional TTS). Not just a
  chat box with leaf tools.
- **Skills** — declarative, hot-reloaded workflow bundles (`data/skills/*/skill.json`) the
  model can run or author (`run_skill` / `save_skill`). Ships with starters such as
  `morning_brief`, `research_brief`, and `slop_mode`.
- **External agent sessions** — spawn/monitor Claude Code (and Codex / generic PTY adapters)
  as live cards; voice/toast when a session needs input; reply from chat or voice.
- **Surfaces** — the AI can compose on-screen content (`ui_control` + SurfaceHost:
  placeholder, image, web/video). Real embedded browser via QtWebEngine with adblock + YouTube clean-mode.
- **Holographic UI** — frameless shell, aurora glass theme, per-section hues, command
  palette (**Ctrl+K**), settings page, notification center + toasts, dashboard HUD.
- **Personalities** — drop-in historical personas (`persona.json` bundles); ships with
  Marcus Aurelius and Ada Lovelace.
- **Vision** — per-camera pipeline over ESP32-CAM / MJPEG: motion gating, person detection
  (YOLOv8n), face recognition (SCRFD + ArcFace), and “where did I last see …” via a VLM.
- **Memory** — long-term semantic memory (vector recall over EmbeddingGemma), daily
  summarizer, per-category retention — wired into the harness prompts and tools.
- **Toolset** — ~25 tools with risk classes: web search/fetch, browser automation, docs &
  lab reports, print, shopping, reminders/tasks, cameras/who's-home, memory, skills,
  agent sessions, UI control.
- **Tiered inference** — resident *Fast* model for live voice (4k ctx, q8 KV on 8 GB
  cards), on-demand Vision/Embedding, honest VRAM budgeter. Heavy local 27B is optional;
  deep work prefers agent-session delegation on Max-Q GPUs.

> **Privacy & security.** Everything runs locally. Ambient listening / face recognition
> default ON but are fully toggleable behind a master kill-switch, with per-category
> retention. The SQLite database is **encrypted at rest** (SQLCipher/AES) with a
> per-install, OS-protected key. See [`docs/PRIVACY.md`](docs/PRIVACY.md).

## Hardware note (this project’s target machine)

Budgeted for **Intel i7-9750H, 32 GB RAM, RTX 2070 Max-Q 8 GB (sm_75)**. Older docs that
mentioned a 3080 Ti are wrong. Resource table:
[`docs/overhaul/04_VOICE_RESOURCES.md`](docs/overhaul/04_VOICE_RESOURCES.md).

## Install (end users)

Grab the latest installer from the repo's **Releases** and run it:

- `Polymath-<version>-win64-cuda-Setup.exe` — NVIDIA GPU build (CUDA, much faster).
- `Polymath-<version>-win64-cpu-Setup.exe` — CPU-only build (works anywhere, slower).

On first launch with no models, Polymath guides you through a model fetch + a GPU/driver
check rather than dropping you into a dead app. Models are **not** bundled (they're ~GBs);
the first-run wizard downloads them. See [`docs/PACKAGING.md`](docs/PACKAGING.md).

## Build from source (developers)

Windows 10/11, MSVC 2022, CMake ≥ 3.25. Native engines (llama.cpp, whisper.cpp, SQLCipher, …)
are vendored and built from source; Qt 6.6, OpenCV, ONNX Runtime and the small vcpkg libs
come from `build/deps` + vcpkg. Full detail in [`docs/BUILD.md`](docs/BUILD.md).

```powershell
# CPU build (no GPU needed) — configures, builds, runs ctest, deploys runtime DLLs
pwsh scripts/build-cpu.ps1

# Fetch the default local models into build/cpu/bin/Release/data/models
pwsh scripts/fetch-models.ps1            # add -Minimal to skip the big optional ones

# GPU / CUDA build (NVIDIA, sm_75 on this machine). Assumes CPU prereqs exist.
pwsh scripts/build-gpu.ps1               # -> build/cuda/bin/Polymath.exe

# Run
build/cuda/bin/Polymath.exe                # or build/cpu/bin/Release/Polymath.exe
```

UI-only visual loop (no CUDA, minutes not tens of minutes):

```powershell
cmake --build build/cpu --config Release --target capture_views
$env:QT_QPA_PLATFORM='offscreen'
build/cpu/bin/Release/capture_views.exe <outdir> [--empty]   # 13 views → PNG
```

Run the test suite: `ctest --test-dir build/cpu -C Release` (**14** suites: core, tools,
audio, agent, vision, inference, memory, privacy, integration, ui, j_phase2, harness,
skills, sessions). CI entry point: `scripts/ci.ps1`.

Package a distributable + build the installer:

```powershell
pwsh scripts/package.ps1 -Flavor cuda    # stages dist/ + a portable zip
& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" /DAppVersion=0.3.0 /DFlavor=cuda scripts/installer/polymath.iss
```

See [`docs/SHIP.md`](docs/SHIP.md) for the full release checklist. Live status and verify
gates: [`docs/STATUS.md`](docs/STATUS.md).

## Repository layout

```
src/core/        shared contracts: EventBus, DB/schema (+ SQLCipher), config, privacy, retention
src/inference/   llama.cpp backend, tiered model manager, VRAM budget, GBNF grammar
src/scheduler/   deep-work task queue, idle detector, proactive engine
src/audio/       capture, VAD-gated wake, whisper ASR, Piper TTS (async workers)
src/vision/      camera workers, motion, YOLO, face recognition, visual memory / finder
src/memory/      SQLite store, vector index, daily summarizer
src/agent/       AgentLoop v2, tools (~25), skills registry, personas
src/sessions/    external agent session service + CLI providers
src/personality/ hot-loadable persona bundle manager
src/app/         AppController facade + main.cpp
src/ui/          Qt Quick (QML) holographic shell, views, models, capture_views
firmware/esp32cam/  ESP32-CAM MJPEG streaming firmware + flashing guide
data/skills/     starter skill.json bundles
docs/            architecture, build, models, privacy, packaging, ship, status
docs/overhaul/   2026-07 overhaul plan + PROGRESS ledger (source of truth for that work)
```

Architecture deep-dive: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).  
Overhaul plan (if contributing to in-flight DAG work): [`docs/overhaul/00_MASTER_PLAN.md`](docs/overhaul/00_MASTER_PLAN.md).
