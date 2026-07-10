# Build status

**Overhaul v0.2.0** (waves A–C + D1/D2 verify). Hardware truth and resource budget
live in [`docs/overhaul/04_VOICE_RESOURCES.md`](overhaul/04_VOICE_RESOURCES.md) §1.
Execution ledger: [`docs/overhaul/PROGRESS.md`](overhaul/PROGRESS.md).

## Hardware (this machine)

| Resource | Value |
|---|---|
| CPU | Intel i7-9750H, 6C/12T (laptop) |
| RAM | 32 GB |
| GPU | **RTX 2070 Max-Q, 8 GB VRAM (sm_75)** |
| Use profile | Dedicated to Polymath + browser/video research |

Older notes that claimed a **RTX 3080 Ti / sm_86** are wrong for this hardware.
Steady-state VRAM budget (Fast @ 4096 ctx + q8 KV + Embedding + OS baseline) ≈
**5.7–6.2 GB** with ~1.9–2.4 GB headroom. **Heavy 27B is parked** (not in the default
fetch); deep work → external agent sessions or the Fast idle queue.

## Build / verify (Wave D)

| Gate | Status | Notes |
|------|--------|--------|
| **D1** CPU Release build + captures | ✅ green | `build/cpu` Release; `capture_views` **13/13** (shell + 12 feature views) |
| **D2** ctest | ✅ green | **14/14** suites (see below) |
| **D3** models + live e2e + resource audit | ⏳ pending | `fetch-models.ps1` + live voice / barge-in / agent_spawn — **not claimed done here** |
| **D4** docs refresh + graphify + tag prep | ✅ this pass | Tag `v0.2.0-overhaul` is orchestrator-owned (not pushed from this node) |

- **CPU build** — `build/cpu` (Visual Studio 2022 generator). Reproduced by
  [`scripts/build-cpu.ps1`](../scripts/build-cpu.ps1).
- **GPU / CUDA build** — `build/cuda` (Ninja + portable CUDA toolkit, **sm_75**).
  Reproduced by [`scripts/build-gpu.ps1`](../scripts/build-gpu.ps1).

`Polymath.exe` is built with MSVC 2022 + Qt 6.6.3 + OpenCV 4.9 + ONNX Runtime 1.17
(CPU) + llama.cpp/whisper.cpp from source. Service threads (each on its own
`QThread`, EventBus only):

Inference · Scheduler · Proactive · Idle · Memory · Agent · Vision · Audio ·
**Sessions** (external CLI agents).

UI-only iteration (no CUDA):

```powershell
cmake --build build/cpu --config Release --target capture_views
$env:QT_QPA_PLATFORM='offscreen'
build/cpu/bin/Release/capture_views.exe <outdir> [--empty]
```

## What the 2026-07 overhaul landed

| Area | Landed |
|------|--------|
| **GUI** | Holographic aurora design system (`Style.qml` tokens only); glass primitives + `Pm*` controls; frameless shell; Settings; command palette (Ctrl+K); toast/bell/notification center; SurfaceHost (placeholder/image/web/video); real QtWebEngine WebSurface with adblock + YouTube clean-mode (D5) |
| **Harness v2** | `AgentLoop` plan → execute → reflect; SQLite goals/plan_steps; token-budgeted context + memory injection; TaskScheduler tool/summarizer dispatch + delivery |
| **Skills** | `SkillRegistry` + `run_skill` / `save_skill`; starters `slop_mode`, `morning_brief`, `research_brief` under `data/skills/` |
| **Agent sessions** | `AgentSessionService` + Claude Code / Codex / generic PTY providers; `SessionsModel` + Agents view; tools `agent_spawn` / `send` / `status` / `stop` / `watch` |
| **Tools** | **25** registered tools with risk classes (read / write_local / external / spend); `ui_control`; improved `browser_drive` (session reuse) |
| **Audio** | VAD-gated wake word; lazy GPU whisper + idle unload; AsrWorker/TtsWorker threads; 16 s capture ring; persistent Piper + sentence streaming; barge-in v1 |
| **Inference** | Fast default **n_ctx=4096** + **KV q8_0**; VramBudget honest for 8 GB Max-Q |

## Tests

`ctest` green: **14/14** suites on the CPU Release tree:

| Suite | Binary |
|-------|--------|
| core | `test_core` |
| tools | `test_tools` |
| audio | `test_audio_e2e` |
| agent | `test_agent_e2e` |
| vision | `test_vision_e2e` |
| inference | `test_inference_e2e` |
| memory | `test_memory_e2e` |
| privacy | `test_privacy_e2e` |
| integration | `test_integration_e2e` |
| ui | `test_ui_e2e` |
| j_phase2 | `test_j_phase2_e2e` |
| harness | `test_harness_e2e` |
| skills | `test_skills` |
| sessions | `test_sessions_e2e` |

Model-gated / live-CLI cases **skip-green** when weights or `claude` are absent — that is
expected until **D3**.

## Module status

| Module | Status |
|--------|--------|
| core, inference, audio, vision, scheduler, memory, agent, personality, sessions, ui/app | ✅ compile + link; CPU verify green (D1/D2) |
| inference (llama.cpp ggml-CUDA) | ✅ GPU path; target **RTX 2070 Max-Q 8 GB (`sm_75`)**, Fast @ 4k + q8 KV |
| AgentLoop v2 + skills + agent sessions | ✅ code + unit/e2e suites; live multi-step voice goals await D3 models |
| VLM (`describeImage` / mtmd) | ✅ built when `LLAMA_BUILD_TOOLS/COMMON=ON`; `mtmd.dll` linked + deployed |
| Piper TTS | ✅ persistent `piper.exe` via QProcess |
| ESP32-CAM firmware | ✅ complete (compile in Arduino IDE) |
| Mobile companion | Parked for this overhaul pass (code retained; not a Wave D gate). Desktop **Settings ▸ Mobile Access** still present. |
| at-rest encryption | ✅ ACTIVE — vendored SQLCipher + OpenSSL; per-install DPAPI-protected key |
| packaging | ✅ portable zips + Inno Setup for CPU & CUDA (`docs/SHIP.md`) — version bump for 0.2.0 is release process |

## Models

`scripts/fetch-models.ps1` downloads the default local set into `data/models/` (see
[`docs/MODELS.md`](MODELS.md)): Gemma 3n E4B (Fast), EmbeddingGemma, Gemma 3 4B + mmproj
(VLM), whisper base/tiny, Piper voices, Silero VAD, openWakeWord, SCRFD, ArcFace, and
**yolov8n.onnx**. **Gemma 3 27B (Heavy) is opt-in** (`-Heavy`); not part of the default
8 GB Max-Q set.

**D3 note:** until models are fetched on this machine, live wake→ASR→LLM→TTS and full
agent-session e2e are **not** claimed green in this doc.

## Honest remaining notes

1. **D3 pending** — model fetch, live voice loop, resource audit screenshots vs 04 §1 budget.
2. **Perception on GPU.** YOLO/SCRFD/ArcFace run on CPU (CPU ORT package). CUDA ORT is a
   drop-in later step; code already requests the CUDA EP and falls back cleanly.
3. **Heavy model parked on 8 GB.** Deep work uses external agent sessions (overhaul 05) or
   the Fast idle queue.
4. **Web surfaces.** **D5 landed:** QtWebEngine installed into the 6.6.3 kit; `WebSurface`
   embeds `WebEngineView` with a shared adblock interceptor and YouTube clean-mode script.
5. **Packaging.** `scripts/package.ps1 -Flavor {cpu,cuda}` produces zips/installers.
   Remaining ship TODOs (code signing, clean-VM smoke) in [`SHIP.md`](SHIP.md).

## How it was built (reproducible)

```powershell
git submodule update --init --recursive   # or scripts/setup-dev.ps1
pwsh scripts/build-cpu.ps1                 # configures + builds build/cpu
pwsh scripts/fetch-models.ps1              # D3: download default local models
pwsh scripts/build-gpu.ps1                 # configures + builds + deploys build/cuda
```

`build-gpu.ps1` builds through a no-space NTFS junction (`C:\pm` → repo) because **nvcc
cannot tolerate a space anywhere in its paths**. See `BUILD.md`.

## Notes from the CUDA bring-up (still valid)

- **nvcc + MSVC `/flag` leak.** Global `add_compile_options` must be gated with
  `$<COMPILE_LANGUAGE:…>` so flags never reach `.cu` files.
- **char8_t split.** Third_party builds at C++17; app modules stay C++20.
- **nvcc + spaces.** Always build through the `C:\pm` junction.
- **Runtime DLLs.** `build-gpu.ps1` deploys fmt/spdlog, OpenCV ffmpeg plugin, and CUDA
  runtime DLLs next to the exe.
