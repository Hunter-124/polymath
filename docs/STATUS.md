# Build status

Both builds are verified: **they compile, link, run the tests, and launch.**

- **CPU build** — `build/cpu` (Visual Studio 2022 generator). Reproduced by
  [`scripts/build-cpu.ps1`](../scripts/build-cpu.ps1).
- **GPU / CUDA build** — `build/cuda` (Ninja generator + portable CUDA 13.2 toolkit,
  `sm_75` for this machine's RTX 2070 Max-Q). Reproduced by
  [`scripts/build-gpu.ps1`](../scripts/build-gpu.ps1).
  **Target GPU: RTX 2070 Max-Q 8 GB (sm_75).** Older notes that claimed a 3080 Ti /
  sm_86 are wrong for this hardware — see [`docs/overhaul/04_VOICE_RESOURCES.md`](overhaul/04_VOICE_RESOURCES.md).

`Polymath.exe` is built with MSVC 2022 + Qt 6.6.3 + OpenCV 4.9 + ONNX Runtime 1.17
(CPU) + llama.cpp/whisper.cpp built from source, and confirmed at runtime to:

- open the database (static sqlite3), start all **8 service threads**
  (Inference, Scheduler, Memory, Proactive, Agent, Idle, Vision, Audio),
- auto-register + load the **Gemma 3n E4B** Fast model, seed + load the modular
  personalities (Marcus Aurelius, Ada Lovelace),
- open the microphone (16 kHz mono capture, whisper ASR transcribing), detect Piper TTS,
- register all **16 agent tools**, load the YOLOv8n / SCRFD / ArcFace ONNX models,
- load the Qt Quick GUI scene and run the event loop,
- degrade gracefully when models/CUDA are absent — **no crashes**.

`ctest` green: **11/11 suites** on both the CPU and CUDA builds — core, tools, audio, agent, vision,
inference, memory, privacy, integration, ui, phase2. The DB is **encrypted at rest** (SQLCipher/AES,
per-install OS-protected key) — confirmed active at runtime. See [`SHIP.md`](SHIP.md).

## GPU build — what was verified (headless, `QT_QPA_PLATFORM=offscreen`)

**Target hardware (this machine):** Intel i7-9750H (6C/12T), 32 GB RAM,
**RTX 2070 Max-Q 8 GB VRAM (sm_75)**. Steady-state budget (Fast @ 4096 ctx + q8 KV +
Embedding + OS baseline) ≈ 5.7–6.2 GB with ~1.9–2.4 GB headroom — see
[`docs/overhaul/04_VOICE_RESOURCES.md`](overhaul/04_VOICE_RESOURCES.md) §1.

Historical bring-up on a different card (RTX 3080 Ti 12 GB, sm_86) confirmed the
ggml-CUDA path end-to-end (~108 tok/s tg16 on Gemma 3n E4B). On this Max-Q:

- Fast model defaults to **n_ctx=4096** + **KV q8_0** (not 8k fp16) so it fits with
  browser/video headroom.
- **Heavy 27B is parked** — not in the default fetch; deep work → agent-session
  delegation or idle Fast queue.
- Whisper ASR is **on-demand** (not idle-resident VRAM).
- **ONNX Runtime stays on CPU** by design (CPU ORT package). The CUDA build accelerates
  **llama/whisper (ggml-cuda)**; perception ONNX is CPU.

## How it was built (reproducible)

```powershell
git submodule update --init --recursive   # or scripts/setup-dev.ps1
pwsh scripts/build-cpu.ps1                 # configures + builds build/cpu
pwsh scripts/fetch-models.ps1              # download the default local models
pwsh scripts/build-gpu.ps1                 # configures + builds + deploys build/cuda (CUDA)
```

`build-gpu.ps1` assumes the CPU prereqs exist (Qt/OpenCV/ONNX + the small vcpkg libs +
the portable CUDA toolkit under `build/deps/cuda/toolkit`). It builds through a no-space
NTFS junction (`C:\pm` → repo) because **nvcc cannot tolerate a space anywhere in its
paths** and the repo lives in `…\Home Assistant`. See `BUILD.md`.

## Module status

| Module | Status |
|--------|--------|
| core, inference, audio, vision, scheduler, memory, agent, personality, ui/app | ✅ compile + link; verified at runtime (CPU **and** CUDA) |
| inference (llama.cpp ggml-CUDA) | ✅ GPU path verified; target card RTX 2070 Max-Q 8 GB (`sm_75`), Fast @ 4k + q8 KV |
| VLM (mtmd / `describeImage`) | ✅ built (`LLAMA_BUILD_TOOLS/COMMON=ON`); `mtmd.dll` linked + deployed |
| Piper TTS | ✅ drives the prebuilt `piper.exe` via QProcess (detected at runtime) |
| ESP32-CAM firmware | ✅ complete (compile in Arduino IDE) |
| Mobile companion | ✅ wired in — `pm_gateway` (QHttpServer+QWebSocket on `0.0.0.0:8765`, device-token auth) inside `Polymath.exe`; `app/` PWA (Capacitor/React) + `cloud/relay/` (opt-in). Desktop **Settings ▸ Mobile Access** pairing QR. Verified: `/api/v1/status`→200, protected routes→401. |
| tests | ✅ 11/11 ctest suites green (CPU + CUDA): core, tools, audio, agent, vision, inference, memory, privacy, integration, ui, phase2 |
| at-rest encryption | ✅ ACTIVE — vendored SQLCipher 4.6.1 + OpenSSL; per-install DPAPI-protected key; plaintext→encrypted migration |
| packaging | ✅ portable zips + Inno Setup installers compile for CPU & CUDA (`docs/SHIP.md`) |

## Models

`scripts/fetch-models.ps1` downloads the default local set into `data/models/` (see
[`docs/MODELS.md`](MODELS.md)): Gemma 3n E4B (Fast), EmbeddingGemma, Gemma 3 4B + mmproj
(VLM), whisper base/tiny, Piper voices, Silero VAD, openWakeWord, SCRFD, ArcFace, and
**yolov8n.onnx**. **Gemma 3 27B (Heavy) is opt-in** (`-Heavy`); not part of the default
8 GB Max-Q set.

## Honest remaining notes

1. **Perception on GPU.** YOLO/SCRFD/ArcFace run on CPU (CPU ORT package). To accelerate
   them, drop in the CUDA ORT package + `onnxruntime_providers_cuda.dll`; the code already
   requests the CUDA EP and falls back cleanly.
2. **Heavy model parked on 8 GB.** Gemma 3 27B Q4 (~16 GB) is not fetched by default.
   Use `-Heavy` only on larger cards; on this machine deep work uses external agent
   sessions (see overhaul 05) or the Fast idle queue.
3. **Packaging.** DONE — `scripts/package.ps1 -Flavor {cpu,cuda}` produces portable zips and the
   Inno Setup installers compile for both flavors (`dist/Polymath-0.1.0-win64-{cpu,cuda}-Setup.exe`).
   Bundles ship without models; the first-run wizard fetches them. Remaining ship TODOs (code
   signing, a clean-VM smoke pass) are tracked in [`SHIP.md`](SHIP.md).


## Notes from the bring-up

The compiler/linker/runtime surfaced (and we fixed) real issues — see the `Build bring-up`
commit and the **KEY GOTCHAS** in `BUILD.md`. The CUDA-specific ones:

- **nvcc + MSVC `/flag` leak.** A global `add_compile_options(/utf-8 /MP /EHsc /Zc:char8_t-)`
  reaches CUDA compiles verbatim (CMake does not wrap COMPILE_OPTIONS in `-Xcompiler`), and
  nvcc misparses a `/flag` as an input file → *"nvcc fatal: A single input file is required
  for a non-link phase."* Fixed by gating the flags with `$<COMPILE_LANGUAGE:…>` so they
  never reach `.cu` files.
- **char8_t split in the llama tree at C++20.** Our global C++20 standard leaked into the
  vendored engines: `llama-chat.cpp` uses `u8""` as `const char*` (needs char8_t **off**)
  while `common/`'s nlohmann/json uses `std::u8string` (needs it **on**) — irreconcilable.
  Fixed by compiling third_party at its native **C++17** (no char8_t at all); our modules
  stay C++20.
- **nvcc + spaces.** Build through the `C:\pm` junction (no admin needed).
- **Runtime DLLs windeployqt misses.** `Polymath.exe` also needs `fmt.dll`, `spdlog.dll`
  (vcpkg), `opencv_videoio_ffmpeg490_64.dll`, and the CUDA `cudart/cublas/cublasLt 64_13`
  DLLs next to the exe; without them the loader hangs before `main()`. `build-gpu.ps1`
  deploys all of these.
