# Models & data layout

All models are **local** and loaded at runtime from `data/models/` (data, not
code — they cannot live inside the exe). Roles are reassignable in the in-app
**Model Manager**; this layout is just the zero-config default that
[`scripts/fetch-models.ps1`](../scripts/fetch-models.ps1) populates.

```
data/models/
├─ llm/            Fast (+ optional Heavy) GGUF LLMs           (llama.cpp)
│    Qwen2.5-7B-Instruct-Q4_K_M.gguf            ← Fast (resident)
│    Qwen2.5-32B-Instruct-Q4_K_M.gguf           ← Heavy (on-demand, optional)
├─ embeddings/     embedding GGUF                              (llama.cpp)
│    nomic-embed-text-v1.5.Q4_K_M.gguf
├─ whisper/        ASR ggml models                            (whisper.cpp)
│    ggml-base.en.bin   ← command  |  ggml-tiny.en.bin ← ambient
├─ piper/          TTS voices (.onnx + .onnx.json)            (Piper)
│    en_GB-alan-medium.onnx(.json)   en_US-amy-medium.onnx(.json)
├─ vlm/            vision-language GGUF + mmproj (optional)    (llama.cpp mtmd)
│    Qwen2-VL-7B-Instruct-Q4_K_M.gguf  mmproj-Qwen2-VL-7B-Instruct-f16.gguf
└─ onnx/           ONNX models                                (ONNX Runtime)
   ├─ wakeword/    openWakeWord: melspectrogram.onnx, embedding_model.onnx,
   │               and one wake model e.g. hey_jarvis_v0.1.onnx
   ├─ vad/         silero_vad.onnx
   ├─ yolo/        yolov8n.onnx  (person detection)
   └─ face/        SCRFD detector + ArcFace embedder (e.g. det_10g.onnx,
                   w600k_r50.onnx from the InsightFace buffalo_l pack)
```

## Getting the models
```powershell
pwsh scripts/fetch-models.ps1            # full set
pwsh scripts/fetch-models.ps1 -Minimal   # skip Heavy LLM + Vision VLM
```

### Multi-file bundles the script can't one-line
- **openWakeWord** (`onnx/wakeword/`): download from the `dscripka/openWakeWord`
  release assets — you need the shared `melspectrogram.onnx` + `embedding_model.onnx`
  plus a wake model (`hey_jarvis_v0.1.onnx`, `alexa_v0.1.onnx`, or a custom one).
  Set the active wake word in **Privacy/Settings** (`audio.wake_word`).
- **Face recognition** (`onnx/face/`): the InsightFace **buffalo_l** pack
  (SCRFD detector + ArcFace `w600k_r50` recognizer), exported to ONNX. Only
  needed if face recognition is enabled in the Privacy panel.

## VRAM budgeting (~8 GB target)
The InferenceManager keeps the **Fast** model resident and loads **Heavy**/**Vision**
on demand, lowering `n_gpu_layers` (CPU offload) to fit. Defaults that fit 8 GB:
Fast 7B Q4_K_M fully on GPU; Vision VLM and Heavy load only when needed and may
spill to CPU (slower — that's the intended "deep think" path).
