# Hearth — a fully-local AI home assistant

Hearth is an always-on, **100% local** AI home assistant for Windows: one C++/Qt Quick
application — no cloud, no telemetry, no account. It listens, sees, remembers, and acts, entirely
on your own machine and GPU.

> The product is **Hearth**. Internally the engine still carries the project's original codename,
> `Polymath`, so you'll see it throughout the code and build files — the `polymath` C++ namespace,
> `pm_*` targets, `POLYMATH_*` build options, the `polymath://` URL scheme, and so on.

- **Voice loop** — wake word → speech-to-text (whisper.cpp) → a local LLM (llama.cpp) → text-to-speech
  (Piper). Push-to-talk or hands-free.
- **Personalities** — the assistant can think as drop-in historical personas (`persona.json` bundles);
  ships with Marcus Aurelius and Ada Lovelace.
- **Vision** — per-camera pipeline over ESP32-CAM / MJPEG streams: motion gating, person detection
  (YOLOv8n), face recognition (SCRFD + ArcFace), and "where did I last see …" object search via a VLM.
- **Memory** — long-term semantic memory (vector recall over EmbeddingGemma), a daily summarizer, and
  per-category retention.
- **Agent toolset** — 17 tools: web search, page fetch, image analysis, document & lab-report drafting,
  printing, shopping lists, reminders/tasks, camera/who's-home, and Chrome browser automation.
- **Tiered inference** — a resident *Fast* model for live voice, plus an on-demand *Heavy* model that
  drains a deep-work queue while the machine is idle, with a VRAM budgeter that fits the card.

> **Privacy & security.** Everything runs locally. Ambient listening / face recognition default ON but
> are fully toggleable behind a master kill-switch, with per-category retention. The SQLite database is
> **encrypted at rest** (SQLCipher/AES) with a per-install, OS-protected key. See [`docs/PRIVACY.md`](docs/PRIVACY.md).

## Install (end users)

Grab the latest installer from the repo's **Releases** and run it:

- `Hearth-<version>-win64-cuda-Setup.exe` — NVIDIA GPU build (CUDA, much faster).
- `Hearth-<version>-win64-cpu-Setup.exe` — CPU-only build (works anywhere, slower).

On first launch with no models, Hearth guides you through a model fetch + a GPU/driver check rather
than dropping you into a dead app. Models are **not** bundled (they're ~GBs); the first-run wizard
downloads them. See [`docs/PACKAGING.md`](docs/PACKAGING.md) for the minimal (~few GB) vs full (~28 GB)
model sets and how to bring your own GGUF/ONNX.

## Build from source (developers)

Windows 10/11, MSVC 2022, CMake ≥ 3.25. Native engines (llama.cpp, whisper.cpp, SQLCipher, …) are
vendored and built from source; Qt 6.6, OpenCV, ONNX Runtime and the small vcpkg libs come from
`build/deps` + vcpkg. Full detail in [`docs/BUILD.md`](docs/BUILD.md).

```powershell
# CPU build (no GPU needed) — configures, builds, runs ctest, deploys runtime DLLs
pwsh scripts/build-cpu.ps1

# Fetch the default local models into build/cpu/bin/Release/data/models
pwsh scripts/fetch-models.ps1            # add -Minimal to skip the big optional ones

# GPU / CUDA build (NVIDIA, sm_86+). Assumes the CPU prereqs above exist.
pwsh scripts/build-gpu.ps1               # -> build/cuda/bin/Hearth.exe

# Run
build/cuda/bin/Hearth.exe                # or build/cpu/bin/Release/Hearth.exe
```

Run the test suite: `ctest --test-dir build/cpu -C Release` (11 suites: core, tools, audio, agent,
vision, inference, memory, privacy, integration, ui, phase2). CI entry point: `scripts/ci.ps1`.

Package a distributable + build the installer:

```powershell
pwsh scripts/package.ps1 -Flavor cuda    # stages dist/ + a portable zip
& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" /DAppVersion=0.1.0 /DFlavor=cuda scripts/installer/polymath.iss
```

See [`docs/SHIP.md`](docs/SHIP.md) for the full release checklist.

## Repository layout

```
src/core/        shared contracts: EventBus, DB/schema (+ SQLCipher), config, privacy, retention
src/inference/   llama.cpp backend, tiered model manager, VRAM budget, GBNF grammar
src/scheduler/   deep-work task queue, idle detector, proactive engine
src/audio/       capture, wake word, VAD, whisper ASR, Piper TTS
src/vision/      camera workers, motion, YOLO, face recognition, visual memory / finder
src/memory/      SQLite store, vector index, daily summarizer
src/agent/       tool registry + 17 tools (web, docs, print, shopping, home, memory, browser_drive)
src/personality/ hot-loadable persona bundle manager
src/app/         AppController facade + main.cpp
src/ui/          Qt Quick (QML) views + C++ view-model glue
firmware/esp32cam/  ESP32-CAM MJPEG streaming firmware + flashing guide
docs/            architecture, build, models, privacy, packaging, ship checklist, status
```

Project history and the parallel build plan live under [`docs/`](docs/) and
[`docs/sessions/`](docs/sessions/).
