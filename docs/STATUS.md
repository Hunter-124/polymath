# Build status

The **CPU build is verified: it compiles, links, runs the tests, and launches.**

`Polymath.exe` was built with MSVC 2022 + Qt 6.6.3 + OpenCV 4.9 + ONNX Runtime
1.17 (CPU) + llama.cpp/whisper.cpp built from source, and confirmed at runtime to:

- open the database (static sqlite3), start all **8 service threads**
  (Inference, Scheduler, Memory, Proactive, Agent, Idle, Vision, Audio),
- seed + load the modular personalities (Marcus Aurelius, Ada Lovelace),
- open the microphone (16 kHz mono capture streaming),
- register all **16 agent tools**,
- load the Qt Quick GUI scene and run the event loop,
- degrade gracefully when models/CUDA are absent — **no crashes**.

`ctest` green: `test_core` and `test_tools` pass with real assertions.

## How it was built (reproducible)
Run [`scripts/build-cpu.ps1`](../scripts/build-cpu.ps1). It uses prebuilt Qt
(via `aqtinstall`), prebuilt OpenCV + ONNX Runtime, a tiny classic-mode vcpkg for
the small libs (nlohmann-json/fmt/spdlog/libsamplerate), and builds
llama.cpp/whisper.cpp from the submodules. See also [`BUILD.md`](BUILD.md).

```powershell
git submodule update --init --recursive   # or scripts/setup-dev.ps1
pwsh scripts/build-cpu.ps1                 # configures + builds build/cpu
pwsh scripts/fetch-models.ps1             # download default local models
build/cpu/bin/Release/Polymath.exe         # run (after windeployqt — see build-cpu.ps1)
```

## Module status

| Module | Status |
|--------|--------|
| core, inference, audio, vision, scheduler, memory, agent, personality, ui/app | ✅ compile + link; backend init verified at runtime |
| ESP32-CAM firmware | ✅ complete (compile in Arduino IDE) |
| tests | ✅ pass (`test_core`, `test_tools`) |

## Deferred / next steps (honest)
1. **CUDA build.** Built CPU-only (the box's CUDA toolkit had no `nvcc` on PATH).
   For GPU: install CUDA 12.x, use `-DGGML_CUDA=ON -DPOLYMATH_USE_CUDA=ON`, and a
   GPU ONNX Runtime package. The code already guards/branches on CUDA.
2. **VLM (`mtmd`) is off.** `LLAMA_BUILD_TOOLS=OFF` for bring-up (fastest-drifting
   API). `describeImage()`/"find my keys" stubs until re-enabled + reconciled.
3. **Piper TTS** not linked (`POLYMATH_HAVE_PIPER` unset) — `speak()` is a no-op.
   Vendor Piper (+espeak-ng) and flip the flag to get voice output.
4. **Models** aren't bundled — run `scripts/fetch-models.ps1`, then assign roles
   in the Model Manager. Until then voice/vision features self-disable.
5. **Packaging.** `windeployqt` gathers the Qt runtime; bundle the ONNX/OpenCV/
   ggml DLLs + `models/` as the portable zip (see [`PACKAGING.md`](PACKAGING.md)).

## Notes from the bring-up
The compiler/linker/runtime surfaced (and we fixed) real issues: a C++20 vs
C++17 `char8_t` clash in the engines, ONNX `AllocatedStringPtr` not being
default-constructible, a `QMetaTypeId` ordering hazard, a `unique_ptr<incomplete>`
destructor, std::string→QString at EventBus boundaries, the QML module needing to
be STATIC so its symbols link, and the vcpkg sqlite3 **DLL** producing spurious
`SQLITE_NOMEM` (fixed by vendoring the amalgamation statically). See the
`Build bring-up` commit for the full list.
