# Packaging, installer & first run

Polymath ships as one primary `Polymath.exe` plus an unavoidable ring of runtime
DLLs and a `data/` folder. This doc covers: the "honest single binary" reality,
how to produce the **portable bundle**, how to wrap it in an **installer**, the
**first-run** flow on a cold box, and the **models strategy** (what to download,
where it goes, and how to bring your own).

---

## The honest "single binary"

The product goal is "one compiled binary with a backend and a Windows GUI." For a
**CUDA + Qt Quick** application a *literal* single `.exe` is not achievable —
some pieces are loaded at runtime as DLLs or data and cannot be embedded:

- **NVIDIA CUDA runtime** — `cudart64_*.dll`, `cublas64_*.dll`, `cublasLt64_*.dll`
  (redistributable DLLs shipped by NVIDIA; not statically linkable).
- **ONNX Runtime** — `onnxruntime.dll` (+ its provider DLLs).
- **Qt runtime** — `Qt6*.dll`, `platforms\`, `qml\`, `imageformats\` (gathered by
  `windeployqt`), unless Qt is configured `-static`.
- **Model files** — GGUF, whisper ggml, Piper voices, the ONNX perception models.
  These are *data*, loaded from `data/models/` at runtime; they are not code and
  cannot live inside the executable.

What we **do** deliver as one coherent unit:

- One primary **`Polymath.exe`** (our code + as many libs statically linked as
  practical; with a static Qt build, Qt folds in too).
- A thin ring of runtime DLLs beside it (Qt, CUDA, ONNX Runtime, OpenCV, fmt/spdlog,
  the VC++ redist DLLs).
- The `data/` folder (`models/`, `personalities/`, `media/`, `logs/`, …).

A static Qt build (`-static`) reduces the DLL ring to just the CUDA/ONNX runtime,
getting as close to "one binary" as the platform allows.

---

## 1. Producing the portable bundle  (`scripts\package.ps1`)

`package.ps1` takes an already-built + deployed tree and stages a clean,
self-contained folder, then zips it.

```powershell
# Build first (see BUILD.md):
pwsh scripts\build-cpu.ps1            # CPU flavour  -> build\cpu\bin\Release
pwsh scripts\build-gpu.ps1            # CUDA flavour -> build\cuda\bin   (deploys runtime)

# Stage + zip the bundle:
pwsh scripts\package.ps1                       # CUDA (default) -> dist\Polymath-<ver>-win64-cuda.zip
pwsh scripts\package.ps1 -Flavor cpu           # CPU bundle
pwsh scripts\package.ps1 -Flavor cuda -NoZip   # stage the folder only (for the installer)
pwsh scripts\package.ps1 -IncludeModels        # self-contained, ~28 GB (rarely wanted)
```

The staged folder contains:

| Piece | What |
|-------|------|
| `Polymath.exe` | the app (dev `llama-*.exe` tools and `.lib/.exp/.pdb` are dropped) |
| Qt runtime | `Qt6*.dll`, `platforms\`, `qml\`, `imageformats\`, `lib\fonts\Inter.ttf` |
| Engine DLLs | `ggml*`, `llama*`, `mtmd`, `whisper`, `onnxruntime`, OpenCV world, `fmt`/`spdlog` |
| OpenSSL | `libcrypto-3-x64.dll` (+ `libssl-3-x64.dll`) — **required**: the crypto backend for the vendored SQLCipher codec that encrypts the DB at rest. Without it the loader fails before `main()` on a clean box. |
| CUDA DLLs | `cudart64_*`, `cublas64_*`, `cublasLt64_*` (CUDA flavour only) |
| VC++ redist | `msvcp140*.dll`, `vcruntime140*.dll`, `concrt140.dll` (runs on a clean box) |
| First-run scripts | `first-run.ps1`, `check-gpu.ps1`, `fetch-models.ps1` |
| `Run-Polymath.cmd` | launcher: drives first-run on a model-less box, else launches the app |
| `README.txt` | top-level instructions |
| `data\models\` | **empty** placeholder + `PUT-MODELS-HERE.txt` (models are NOT bundled) |

Without `-IncludeModels`, models are deliberately omitted — the default set is
~28 GB. The bundle ships the fetcher + wizard so a cold-started user is *guided*
to download them, never dropped into a model-less app.

---

## 2. Installer  (Inno Setup — `scripts\installer\polymath.iss`)

The installer **wraps the staged bundle**; it does not rebuild it. Inno Setup is
chosen over NSIS for its clean handling of a pre-staged folder, per-user installs,
and first-class code-signing hooks.

```powershell
# 1. Stage the bundle as a folder (not a zip):
pwsh scripts\package.ps1 -Flavor cpu -NoZip           # -> dist\Polymath-<ver>-win64-cpu\

# 2. Compile the installer (Inno Setup 6 / ISCC.exe). Use the call operator (&) and
#    quote the .iss path — the repo lives under "...\Home Assistant" (a space), and
#    the /D defines must each be their own token:
& "C:\Users\nigga\AppData\Local\Programs\Inno Setup 6\ISCC.exe" `
    /DAppVersion=0.1.0 /DFlavor=cpu `
    "C:\Users\nigga\Desktop\Home Assistant\scripts\installer\polymath.iss"
# -> dist\Polymath-0.1.0-win64-cpu-Setup.exe   (~67.7 MB, lzma2/max)
```

Defaults are `AppVersion=0.1.0`, `Flavor=cuda`; override either with `/D` (use
`/DFlavor=cpu` for the CPU bundle).

**Status (2026-06-09): the CPU installer COMPILES and was verified.** Inno Setup 6
is installed at `C:\Users\nigga\AppData\Local\Programs\Inno Setup 6\ISCC.exe` (the
per-user winget install location — note it is **not** the `Program Files (x86)` path
the upstream docs assume). ISCC produced
`dist\Polymath-0.1.0-win64-cpu-Setup.exe` and a silent per-user install
(`/VERYSILENT /CURRENTUSER /DIR=…`) was confirmed to land `Polymath.exe`,
`libcrypto-3-x64.dll`, the Qt runtime + offscreen plugin, and an empty
`data\models\`, launch offscreen, then uninstall cleanly (leaving user models). The
portable zip from `package.ps1` remains the install-free fallback. On a box without
Inno Setup, install it with `winget install JRSoftware.InnoSetup` (or
<https://jrsoftware.org/isdl.php>).

> A clean Win10/11 VM pass is still recommended before shipping to verify SmartScreen
> behaviour on the unsigned download and a truly bare image with no VC++ redist. The
> CUDA-flavour installer is authored (`/DFlavor=cuda`) but awaits a GPU build to stage.

The installer:
- installs to `{autopf}\Polymath` (Program Files) or, with `PrivilegesRequired=lowest`,
  a per-user dir without a UAC prompt;
- creates Start-menu entries for the app **and** the first-run setup;
- offers, post-install, to run the first-run wizard (GPU check + guided model
  download) or to launch the app;
- ensures `data\models\` and `data\logs\` exist;
- on uninstall, removes logs but **leaves the user's downloaded models alone**
  (they're large and the user paid the bandwidth).

### Code signing (ships unsigned for now)

Unsigned, SmartScreen shows "Windows protected your PC" on first launch
(*More info → Run anyway*). To sign, get an Authenticode cert (OV/EV from a CA; an
**EV** cert clears SmartScreen reputation fastest) and sign **both** `Polymath.exe`
and the installer, always timestamping so signatures outlive the cert:

```powershell
signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `
    /f mycert.pfx /p <pw> "dist\Polymath-<ver>-win64-<flavor>\Polymath.exe"
# re-stage so the signed exe is the one packaged, build the installer, then:
signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `
    /f mycert.pfx /p <pw> "dist\Polymath-<ver>-win64-<flavor>-Setup.exe"
```

To sign automatically during compile, register a sign tool in the Inno Setup IDE
(*Tools → Configure Sign Tools*) and uncomment `SignTool=signtool` in the `.iss`
`[Setup]` section. EV certs usually live on an HSM/token, so CI signing uses the
token's CSP rather than a `.pfx`. (See the header of `polymath.iss` for the same
recipe inline.)

### Per-machine vs portable data location

The app resolves `data/` **beside `Polymath.exe`** (`src\app\main.cpp`
`resolveAppRoot()`). That keeps the portable bundle truly portable. For a
multi-user / Program-Files install where users can't write under the install dir,
point the app at `%LOCALAPPDATA%\Polymath` instead — that requires a small backend
change (`resolveAppRoot()` to prefer an env/`QStandardPaths` location); it is
filed as a contract request rather than done here (this card owns `scripts\` +
`docs\`, not `src\`). Today the installer defaults to a writable location so the
portable data layout keeps working.

---

## 3. First run (cold box)  (`first-run.ps1` + `check-gpu.ps1`)

On a fresh box `data\models\` is empty, so the first thing a user needs is models.
The flow never drops the user into a dead, self-disabled app:

1. **Launcher** (`Run-Polymath.cmd`): if there's no Fast LLM under
   `data\models\llm\*.gguf`, it runs the first-run wizard; otherwise it launches
   the app directly.
2. **Wizard** (`first-run.ps1`):
   - locates the app + `data\models\`;
   - if models already exist, just offers to launch (idempotent — safe to re-run);
   - runs the **GPU/driver check** (`check-gpu.ps1`) and tells the user whether the
     CUDA build will accelerate inference or it'll run on CPU;
   - offers a model set — **Minimal** (~3.5 GB), **Full** (~28 GB), or **Skip**
     (bring your own / do it later) — then drives `fetch-models.ps1`;
   - launches `Polymath.exe`.
3. **In-app safety net** (from card F): even if the user skips the wizard and
   launches anyway, `AppController::initialize()` always succeeds without models;
   the Dashboard shows a **cold-start banner** and the **Model Manager** shows a
   no-models guide pointing at the fetcher. The app self-disables only the
   *features* whose model is absent (chat/agent need the Fast LLM; voice needs
   whisper/Piper; vision needs the ONNX detectors) — it never silently dies.

`check-gpu.ps1` returns a verdict object (`HasNvidiaGpu`, `DriverVersion`, `GpuName`,
`VramTotalMB`, `VramFreeMB`, `Recommend` = `cuda`|`cpu`, `Notes`). No GPU is **not**
an error — the CPU build runs everywhere, just slower.

Non-interactive / CI usage:

```powershell
pwsh scripts\first-run.ps1 -NonInteractive -Choice skip -NoLaunch   # exercise the cold path
pwsh scripts\check-gpu.ps1                                          # standalone GPU report
```

---

## 4. Models strategy

Models live under `data\models\` in the exact layout the C++ loaders read from
(do not rename — the paths are hard-referenced). Roles are auto-registered from
disk on launch; you reassign them in the in-app Model Manager.

### The two sets

| Set | Size | Contents | What works |
|-----|------|----------|------------|
| **Minimal** (`-Minimal`) | ~3.5 GB | Fast LLM (Gemma 3n E4B Q4) + EmbeddingGemma + whisper base/tiny + Piper voices + Silero VAD + openWakeWord + YOLOv8n + SCRFD + ArcFace | Chat, agent/tools, voice (ASR+TTS+wake), memory/semantic search, camera person + face detection |
| **Full** (default) | ~28 GB | Minimal **plus** Gemma 3 27B Q4 (Heavy/deep-work) **plus** Gemma 3 4B Q4 + mmproj (Vision VLM) | Adds on-demand deep-work reasoning and image understanding (VLM Q&A) |

Get the minimal set running fast, then add Heavy/Vision later — the app
self-disables those two roles until their GGUFs appear.

```powershell
pwsh scripts\fetch-models.ps1 -Root .\data -Minimal   # ~3.5 GB starter set
pwsh scripts\fetch-models.ps1 -Root .\data            # full ~28 GB set
pwsh scripts\fetch-models.ps1 -Root .\data -NoHeavy   # full minus the 27B (~12 GB)
pwsh scripts\fetch-models.ps1 -Root .\data -NoVlm     # full minus the vision VLM
pwsh scripts\fetch-models.ps1 -Root .\data -NoLLM     # only perception/voice (no GGUF LLMs)
```

### Where each file goes (the layout)

```
data\models\
  llm\        <fast>.gguf          (required — resident chat/agent model; Gemma 3n E4B Q4_K_M)
              <heavy>.gguf         (optional — on-demand deep-work; Gemma 3 27B Q4_K_M)
  vlm\        <vision>.gguf        (optional — image understanding; Gemma 3 4B Q4_K_M)
              mmproj-*.gguf        (the multimodal projector that pairs with the VLM)
  embeddings\ <embed>.gguf         (memory / semantic search; EmbeddingGemma 300M Q8_0)
  whisper\    ggml-base.en.bin     ggml-tiny.en.bin            (speech-to-text)
  piper\<voice>\<voice>.onnx(.json)                            (text-to-speech)
  vad\        silero_vad.onnx                                  (voice-activity detection)
  wakeword\   melspectrogram.onnx  embedding_model.onnx  <wake>.onnx   (wake word)
  yolov8n.onnx                                                 (person detection)
  scrfd_500m.onnx   arcface_r100.onnx                          (face detect + recognise)
```

The LLM GGUFs in `llm\`, `vlm\`, `embeddings\` are **auto-registered** on launch;
the perception/voice ONNX/bin files are loaded from their fixed paths above.

### Bring your own GGUF/ONNX

You don't have to use `fetch-models.ps1` — drop your own files into the layout:

- **LLM**: any llama.cpp-compatible GGUF into `llm\` (Fast) — the smallest, fastest
  instruct model you have; the resident model should fit your VRAM/RAM budget. Add a
  larger GGUF to `llm\` for the Heavy role and assign it in the Model Manager.
- **Vision VLM**: a GGUF **plus its matching `mmproj-*.gguf`** into `vlm\` (both are
  required for image understanding).
- **Embeddings**: an embedding GGUF into `embeddings\`.
- **ASR**: a whisper.cpp `ggml-*.bin` into `whisper\`.
- **TTS**: a Piper voice (`.onnx` + `.onnx.json`) into `piper\<voice>\`.
- **Perception**: ONNX detectors named exactly `yolov8n.onnx`, `scrfd_500m.onnx`,
  `arcface_r100.onnx` (interface-compatible exports — see `fetch-models.ps1`).

The app self-disables any feature whose model is absent and surfaces that in the
Model Manager, so a partial set is always safe.

### Notes / caveats

- Several upstream HF mirrors (the official `google/*` QAT repos, the Xenova/
  onnx-community YOLO mirrors) now 401 for anonymous downloads. `fetch-models.ps1`
  uses ungated mirrors (`unsloth/*`, a GitHub-hosted YOLOv8n export); if a download
  401s or 404s it warns and continues — re-run to resume (curl `-C -`).
- `-IncludeModels` on `package.ps1` bakes the resolved `data\models\` tree into the
  bundle for an air-gapped, ~28 GB self-contained zip. Rarely wanted; the guided
  fetch is the normal path.
