# Packaging — the honest "single binary"

The product goal is "one compiled binary with a backend and a Windows GUI."
For a **CUDA + Qt Quick** application a *literal* single `.exe` is not achievable,
because some pieces are loaded at runtime as DLLs or data and cannot be embedded:

- **NVIDIA CUDA runtime** — `cudart64_*.dll`, `cublas64_*.dll`, `cublasLt64_*.dll`
  (redistributable DLLs shipped by NVIDIA; not statically linkable).
- **ONNX Runtime (GPU)** — `onnxruntime.dll` (+ its CUDA EP provider DLL).
- **Model files** — GGUF, whisper ggml, Piper voices, and the ONNX models are
  *data*, loaded from `data/models/` at runtime. They are not code and cannot
  live inside the executable.

What we **do** deliver as a single coherent unit:

- One primary **`Polymath.exe`** (our code + as many libs statically linked as
  practical; with a static Qt build, Qt itself folds in too).
- A thin ring of unavoidable runtime DLLs beside it (CUDA, ONNX Runtime; Qt DLLs
  if using a shared Qt build — run `windeployqt` to gather them).
- The `data/` folder (`models/`, `personalities/`, `media/`, …).

### Producing the bundle
```powershell
# 1. Build (see BUILD.md)
cmake --build --preset cuda-release

# 2. Gather Qt runtime + QML next to the exe (shared-Qt builds)
windeployqt --qmldir src/ui/qml build/cuda-release/bin/Polymath.exe

# 3. Copy CUDA + ONNX Runtime DLLs next to the exe
#    (onnxruntime.dll is auto-copied by the build; add CUDA DLLs from the toolkit bin)

# 4. Zip the bin/ folder -> Polymath-portable.zip   (or wrap with an installer)
```

A static Qt build (configure Qt with `-static`) reduces the DLL ring to just the
CUDA/ONNX runtime, getting as close to "one binary" as the platform allows.
An optional Inno Setup / WiX installer can present it as a single download.
