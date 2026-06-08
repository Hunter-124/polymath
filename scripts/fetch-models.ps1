<#
.SYNOPSIS
  Downloads Polymath's default LOCAL model set (Gemma-based) into data/models/,
  in the exact layout the app loads from.

.DESCRIPTION
  LLMs are Gemma (per preference): Gemma 3n E4B (fast/resident), Gemma 3 27B QAT
  (heavy/deep-work), Gemma 3 4B QAT + mmproj (vision VLM), EmbeddingGemma (memory).
  Plus whisper ASR, Piper voices, and the ONNX perception models. All local + free.

.PARAMETER Root      App data root (default: .\data next to the repo).
.PARAMETER NoHeavy   Skip the big 27B heavy model (~16 GB).
.PARAMETER NoLLM     Skip all GGUF LLMs (just perception/voice models).

.EXAMPLE  pwsh scripts/fetch-models.ps1
#>
[CmdletBinding()]
param([string]$Root = (Join-Path $PSScriptRoot '..\data'), [switch]$NoHeavy, [switch]$NoLLM)

$ErrorActionPreference = 'Stop'
$models = Join-Path $Root 'models'
$HF = 'https://huggingface.co'

# Layout the C++ code reads from (do not rename — paths are hard-referenced):
#   models/llm, models/vlm, models/embeddings        (LLM GGUFs; auto-registered)
#   models/whisper/*.bin                              (ASR)
#   models/piper/<voice>/<voice>.onnx(.json)          (TTS)
#   models/vad/silero_vad.onnx                        (VAD)
#   models/wakeword/{melspectrogram,embedding_model,hey_jarvis}.onnx
#   models/yolov8n.onnx  models/scrfd_500m.onnx  models/arcface_r100.onnx  (vision)
@('llm','vlm','embeddings','whisper','vad','wakeword','piper') |
  ForEach-Object { New-Item -ItemType Directory -Force (Join-Path $models $_) | Out-Null }

function Fetch($url, $dest) {
  if (Test-Path $dest) { Write-Host "  [skip] $(Split-Path $dest -Leaf)" -ForegroundColor DarkGray; return }
  New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
  Write-Host "  [get ] $(Split-Path $dest -Leaf)" -ForegroundColor Cyan
  # curl with resume (-C -) handles the multi-GB GGUFs better than Invoke-WebRequest.
  & curl.exe -L --fail --retry 3 -C - -o $dest $url
  if ($LASTEXITCODE -ne 0) { Write-Warning "    failed: $url" }
}

if (-not $NoLLM) {
  Write-Host "Gemma LLMs ->" -ForegroundColor Green
  # Fast (resident): Gemma 3n E4B, Q4_K_M (~4 GB) — efficient, big-VRAM headroom.
  Fetch "$HF/unsloth/gemma-3n-E4B-it-GGUF/resolve/main/gemma-3n-E4B-it-Q4_K_M.gguf" "$models/llm/gemma-3n-E4B-it-Q4_K_M.gguf"
  # Vision VLM: Gemma 3 4B Q4 + projector (multimodal). Ungated unsloth mirror
  # (the official google/* QAT repos are gated and 401 without an HF token).
  Fetch "$HF/unsloth/gemma-3-4b-it-GGUF/resolve/main/gemma-3-4b-it-Q4_K_M.gguf" "$models/vlm/gemma-3-4b-it-Q4_K_M.gguf"
  Fetch "$HF/unsloth/gemma-3-4b-it-GGUF/resolve/main/mmproj-F16.gguf"            "$models/vlm/mmproj-gemma-3-4b-f16.gguf"
  # Embeddings: EmbeddingGemma 300M Q8 (~300 MB).
  Fetch "$HF/ggml-org/embeddinggemma-300M-GGUF/resolve/main/embeddinggemma-300M-Q8_0.gguf" "$models/embeddings/embeddinggemma-300M-Q8_0.gguf"
  # Heavy (on-demand deep-work): Gemma 3 27B Q4_K_M (~16 GB) — partial offload.
  if (-not $NoHeavy) {
    Fetch "$HF/unsloth/gemma-3-27b-it-GGUF/resolve/main/gemma-3-27b-it-Q4_K_M.gguf" "$models/llm/gemma-3-27b-it-Q4_K_M.gguf"
  }
}

Write-Host "Whisper ASR ->" -ForegroundColor Green
Fetch "$HF/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin" "$models/whisper/ggml-base.en.bin"
Fetch "$HF/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin" "$models/whisper/ggml-tiny.en.bin"

Write-Host "Piper voices ->" -ForegroundColor Green
foreach ($v in @(
  @{ id='en_GB-alan-medium';      path='en/en_GB/alan/medium' },
  @{ id='en_US-amy-medium';       path='en/en_US/amy/medium' })) {
  Fetch "$HF/rhasspy/piper-voices/resolve/main/$($v.path)/$($v.id).onnx"      "$models/piper/$($v.id)/$($v.id).onnx"
  Fetch "$HF/rhasspy/piper-voices/resolve/main/$($v.path)/$($v.id).onnx.json" "$models/piper/$($v.id)/$($v.id).onnx.json"
}

Write-Host "VAD + wake word ->" -ForegroundColor Green
Fetch "https://github.com/snakers4/silero-vad/raw/master/src/silero_vad/data/silero_vad.onnx" "$models/vad/silero_vad.onnx"
$oww = 'https://github.com/dscripka/openWakeWord/releases/download/v0.5.1'
Fetch "$oww/melspectrogram.onnx"   "$models/wakeword/melspectrogram.onnx"
Fetch "$oww/embedding_model.onnx"  "$models/wakeword/embedding_model.onnx"
Fetch "$oww/hey_jarvis_v0.1.onnx"  "$models/wakeword/hey_jarvis.onnx"   # default wake phrase

Write-Host "Vision (YOLO + face) ->" -ForegroundColor Green
Fetch "$HF/Xenova/yolov8n/resolve/main/onnx/model.onnx" "$models/yolov8n.onnx"
# InsightFace SCRFD detector + ArcFace recognizer (ONNX, buffalo_l pack). Named
# to match the code; the architectures are interface-compatible with the loaders.
Fetch "$HF/immich-app/buffalo_l/resolve/main/detection/model.onnx"   "$models/scrfd_500m.onnx"
Fetch "$HF/immich-app/buffalo_l/resolve/main/recognition/model.onnx" "$models/arcface_r100.onnx"

Write-Host "`nDone. Launch Polymath — LLMs auto-register on first run (Model Manager to adjust roles)." -ForegroundColor Green
