# Building Hearth

## Prerequisites
- Windows 10/11 x64.
- **Visual Studio 2022** (Desktop C++ workload) or Build Tools + Ninja.
- **CMake ≥ 3.25**.
- **NVIDIA CUDA Toolkit 12.x** (for the CUDA presets; skip for `cpu-release`).
- **vcpkg** — set `VCPKG_ROOT` to its location.
- **ONNX Runtime (GPU)** — download `onnxruntime-win-x64-gpu-*.zip` from the
  ONNX Runtime releases, extract it, and set `ONNXRUNTIME_ROOT` to the extracted
  folder (the one containing `include/` and `lib/`).

## 1. Clone with submodules
```powershell
git submodule update --init --recursive
```
This fetches `third_party/{llama.cpp, whisper.cpp, piper, hnswlib, miniaudio}`.

## 2. Configure + build
```powershell
$env:VCPKG_ROOT     = "C:\dev\vcpkg"
$env:ONNXRUNTIME_ROOT = "C:\dev\onnxruntime-gpu"
cmake --preset cuda-release
cmake --build --preset cuda-release
```
The executable lands in `build/cuda-release/bin/Hearth.exe`.

Presets: `cuda-release`, `cuda-debug`, `cpu-release` (no GPU — small models, slow).

## 3. Models (data — not in the binary)
Create `build/cuda-release/bin/data/models/` and place:
- a **fast** GGUF (e.g. Qwen2.5-7B-Instruct Q4_K_M),
- optionally a **heavy** GGUF, a **vision** GGUF + its `mmproj`, an **embedding** GGUF,
- `whisper/` ggml model (e.g. `ggml-base.en.bin`),
- `piper/` voices (the ids referenced by personalities),
- ONNX models for wake word, VAD, YOLO, and face recognition.

Then register the LLMs/roles in the **Model Manager** view on first run.

## 4. Tests
```powershell
ctest --preset cuda-release
```

## Notes
- First vcpkg configure is slow (it builds Qt, OpenCV, …). Subsequent builds are
  incremental.
- llama.cpp/whisper.cpp are built with `GGML_CUDA=ON` via the preset.
- See [PACKAGING.md](PACKAGING.md) for producing the portable distributable.
