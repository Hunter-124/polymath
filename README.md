# Polymath — Local AI Home Assistant

A fully-local, always-on AI home assistant for Windows. One C++/Qt Quick application that:

- Listens for a **wake word**, transcribes speech (whisper.cpp), thinks with a **local llama.cpp**
  model, and speaks back (Piper TTS).
- Can **think as modular historical personalities** (drop-in `persona.json` bundles).
- Perceives the home through **ESP32-CAM** video: motion, person & face detection, "find my keys"
  object search.
- Keeps **long-term memory**, proactively reminds (vitamins, dinner) and suggests next-day tasks.
- Wields an **agent toolset**: web search, image analysis, document/lab-report drafting, printing,
  shopping lists, and (phase 2) browser automation.
- Uses **tiered inference**: a resident *fast* model for live voice + an on-demand *heavy* model
  that drains a queue of deep-work tasks while the machine is idle.

> **Privacy:** everything runs locally. No telemetry. Ambient listening and face recognition are
> ON by default but fully toggleable, with retention controls and optional at-rest encryption.

See [`docs/`](docs/) for setup, the ESP32-CAM flashing guide, the architecture overview, and the
[approved plan](docs/PLAN.md).

## Quickstart

```powershell
# 1. Bootstrap toolchain (submodules, vcpkg, configure). Pass -OnnxRoot to your
#    extracted onnxruntime-win-x64-gpu folder.
pwsh scripts/setup-dev.ps1 -OnnxRoot C:\dev\onnxruntime-gpu

# 2. Build
cmake --build --preset cuda-release      #  -> build/cuda-release/bin/Polymath.exe

# 3. Fetch the default local models into data/models/
pwsh scripts/fetch-models.ps1            # add -Minimal to skip the big optional ones

# 4. Run, then assign model roles in the Model Manager view.
build/cuda-release/bin/Polymath.exe
```

No GPU? Use `-Preset cpu-release` in step 1 and `--preset cpu-release` in step 2 (small models, slower).

## Building (overview)

Requires: Windows 10/11, an NVIDIA GPU (CUDA 12.x), CMake ≥ 3.25, a recent MSVC, and `vcpkg`.
Native sub-engines (llama.cpp, whisper.cpp, piper) are vendored as git submodules and built with
CUDA. ONNX Runtime (GPU) and the model files are downloaded separately into `models/`. Full detail
in [`docs/BUILD.md`](docs/BUILD.md); model layout in [`docs/MODELS.md`](docs/MODELS.md).

```powershell
git submodule update --init --recursive
cmake --preset cuda-release
cmake --build --preset cuda-release
```

The build produces `Polymath.exe`. Because this is a CUDA + Qt Quick app, a *literal* single `.exe`
is not possible — the deliverable is `Polymath.exe` + a small set of runtime DLLs (CUDA, ONNX
Runtime, Qt) + a `models/` data folder, shipped as a portable bundle. See
[`docs/BUILD.md`](docs/BUILD.md) and [`docs/PACKAGING.md`](docs/PACKAGING.md).

## Repository layout

```
src/core/        shared contracts: EventBus, DB/schema, config, IModelBackend, ITool
src/inference/   llama.cpp backend, tiered model manager, VRAM budget, GBNF grammar
src/scheduler/   deep-work task queue, idle detector, proactive engine
src/audio/       capture, wake word, VAD, whisper ASR, Piper TTS
src/vision/      camera workers, motion, YOLO, face recognition, visual memory / finder
src/memory/      SQLite store, vector index, daily summarizer
src/agent/       tool registry + tools (web, docs, print, shopping, home, memory)
src/personality/ hot-loadable persona bundle manager
src/app/         AppController facade + main.cpp
src/ui/          Qt Quick (QML) views + C++ view-model glue
firmware/esp32cam/  ESP32-CAM MJPEG streaming firmware + flashing guide
assets/personalities/  starter historical personas
```
