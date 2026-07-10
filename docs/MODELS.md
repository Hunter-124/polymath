# Models & data layout

All models are **local** and loaded at runtime from `data/models/` (data, not
code — they cannot live inside the exe). Roles are reassignable in the in-app
**Model Manager**; this layout is the zero-config default that
[`scripts/fetch-models.ps1`](../scripts/fetch-models.ps1) populates.

**Hardware target (this machine):** i7-9750H, 32 GB RAM, **RTX 2070 Max-Q 8 GB VRAM**.
Resource budget and residency rules: [`docs/overhaul/04_VOICE_RESOURCES.md`](overhaul/04_VOICE_RESOURCES.md) §1–2.

```
data/models/
├─ llm/            Fast GGUF LLM (llama.cpp)
│    gemma-3n-E4B-it-Q4_K_M.gguf          ← Fast (resident, 4k ctx, q8 KV)
│    gemma-3-27b-it-Q4_K_M.gguf           ← Heavy (PARKED; only with -Heavy)
├─ embeddings/     embedding GGUF (llama.cpp)
│    embeddinggemma-300M-Q8_0.gguf
├─ vlm/            vision-language GGUF + mmproj (on-demand)
│    gemma-3-4b-it-Q4_K_M.gguf
│    mmproj-gemma-3-4b-f16.gguf
├─ whisper/        ASR ggml models (whisper.cpp)
│    ggml-base.en.bin   ← command (GPU, on-demand)
│    ggml-tiny.en.bin   ← ambient only if enabled
├─ piper/          TTS voices (.onnx + .onnx.json)
│    en_US-amy-medium/   en_GB-alan-medium/
├─ vad/            silero_vad.onnx
├─ wakeword/       openWakeWord: melspectrogram, embedding_model, hey_jarvis
├─ yolov8n.onnx    person detection (vision)
├─ scrfd_500m.onnx face detector
└─ arcface_r100.onnx face embedder
```

## Role table (single source of truth)

| Role | Model | Size | Residency |
|------|--------|------|-----------|
| Fast | gemma-3n-E4B-it-Q4_K_M.gguf | ~4 GB | resident, **4096 ctx**, **KV q8_0** |
| Embedding | embeddinggemma-300M-Q8_0.gguf | ~0.3 GB | resident |
| Vision | gemma-3-4b-it-Q4_K_M + mmproj-f16 | ~3.5 GB | on-demand (evict Fast first) |
| Heavy | — (parked) | — | re-add with `fetch-models.ps1 -Heavy` for ≥16 GB cards |
| ASR | whisper ggml-base.en (+ tiny.en if ambient) | 140 / 75 MB | on-demand GPU |
| TTS | **Kokoro-82M** (neural, default `af_sky`) via ONNX; Piper fallback | ~300 MB | persistent CPU process (does not steal LLM VRAM) |
| Wake / VAD | openWakeWord hey_jarvis trio + silero_vad | tiny | CPU resident |

## Getting the models

```powershell
pwsh scripts/fetch-models.ps1            # default base set (~8 GB: Fast + embed + VLM + voice)
pwsh scripts/fetch-models.ps1 -Minimal   # skip VLM (~3.5–4 GB): Fast + embed + voice
pwsh scripts/fetch-models.ps1 -Heavy     # also fetch Gemma 3 27B (~16 GB extra; not for 8 GB cards)
pwsh scripts/fetch-models.ps1 -NoVlm     # base set without vision VLM
pwsh scripts/fetch-models.ps1 -NoLLM     # perception / voice only
```

### Layout notes
- Paths above match what the C++ loaders hard-reference — do not rename directories.
- **openWakeWord**: `melspectrogram.onnx` + `embedding_model.onnx` + `hey_jarvis.onnx`
  (fetched by the script). Active wake phrase is set in Privacy/Settings (`audio.wake_word`).
- **Face recognition**: InsightFace buffalo_l SCRFD + ArcFace ONNX (fetched as
  `scrfd_500m.onnx` / `arcface_r100.onnx`). Only needed when face recognition is enabled.

## VRAM budgeting (8 GB RTX 2070 Max-Q)

Steady-state target: **Fast + Embedding + OS/browser baseline ≈ 5.7–6.2 GB**, leaving
~1.9–2.4 GB headroom for browser/video decode. Whisper (~150 MiB) loads on wake and
unloads after idle. Vision never co-resides with Fast (evict-then-load).

**Heavy 27B is parked** on this machine — deep work goes to external agent sessions
(Claude Code / Codex) or the idle-time Fast queue. See overhaul plan §3 decisions.
