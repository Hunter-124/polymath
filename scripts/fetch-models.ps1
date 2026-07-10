<#
.SYNOPSIS
  Downloads Polymath's default LOCAL model set (Gemma-based) into data/models/,
  in the exact layout the app loads from.

.DESCRIPTION
  Default set matches docs/overhaul/04_VOICE_RESOURCES.md section 2 and docs/MODELS.md:
  Gemma 3n E4B (fast/resident), EmbeddingGemma, Gemma 3 4B + mmproj (vision),
  whisper ASR, Piper voices, ONNX perception. Heavy 27B is PARKED -- pass -Heavy
  only on machines with enough VRAM/RAM (not the 8 GB Max-Q target).

.PARAMETER Root      App data root (default: .\data next to the repo).
.PARAMETER Heavy     Also fetch Gemma 3 27B Q4 (~16 GB). Off by default.
.PARAMETER NoVlm     Skip the vision VLM (Gemma 3 4B + mmproj, ~3.5 GB).
.PARAMETER NoLLM     Skip all GGUF LLMs (just perception/voice models).
.PARAMETER Minimal   "Get running fast" subset: Fast + Embedding + whisper +
                     VAD/wake + Piper + vision ONNX. Skips VLM (and Heavy).
                     Equivalent to -NoVlm (Heavy already off by default).

.EXAMPLE  pwsh scripts/fetch-models.ps1            # base set (~8 GB, includes VLM)
.EXAMPLE  pwsh scripts/fetch-models.ps1 -Minimal   # minimal subset (~3.5-4 GB)
.EXAMPLE  pwsh scripts/fetch-models.ps1 -Heavy     # base + 27B for big cards
#>
[CmdletBinding()]
param(
  [string]$Root = $(
    if ($PSScriptRoot) { Join-Path $PSScriptRoot '..\data' }
    else { Join-Path (Get-Location) 'data' }
  ),
  [switch]$Heavy, [switch]$NoVlm, [switch]$NoLLM, [switch]$Minimal)

# -Minimal skips the optional vision VLM. Heavy stays opt-in via -Heavy.
if ($Minimal) { $NoVlm = $true }

$ErrorActionPreference = 'Stop'
$models = Join-Path $Root 'models'
$HF = 'https://huggingface.co'

# Layout the C++ code reads from (do not rename -- paths are hard-referenced):
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
  # Fast (resident): Gemma 3n E4B, Q4_K_M (~4 GB) -- 4k ctx + q8 KV on 8 GB cards.
  Fetch "$HF/unsloth/gemma-3n-E4B-it-GGUF/resolve/main/gemma-3n-E4B-it-Q4_K_M.gguf" "$models/llm/gemma-3n-E4B-it-Q4_K_M.gguf"
  # Vision VLM: Gemma 3 4B Q4 + projector (multimodal). Ungated unsloth mirror
  # (the official google/* QAT repos are gated and 401 without an HF token).
  if (-not $NoVlm) {
    Fetch "$HF/unsloth/gemma-3-4b-it-GGUF/resolve/main/gemma-3-4b-it-Q4_K_M.gguf" "$models/vlm/gemma-3-4b-it-Q4_K_M.gguf"
    Fetch "$HF/unsloth/gemma-3-4b-it-GGUF/resolve/main/mmproj-F16.gguf"            "$models/vlm/mmproj-gemma-3-4b-f16.gguf"
  }
  # Embeddings: EmbeddingGemma 300M Q8 (~300 MB).
  Fetch "$HF/ggml-org/embeddinggemma-300M-GGUF/resolve/main/embeddinggemma-300M-Q8_0.gguf" "$models/embeddings/embeddinggemma-300M-Q8_0.gguf"
  # Heavy (parked on 8 GB cards): Gemma 3 27B Q4_K_M (~16 GB) -- opt-in only.
  if ($Heavy) {
    Write-Host "  [Heavy] fetching Gemma 3 27B (~16 GB) -- not for 8 GB VRAM targets" -ForegroundColor Yellow
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
# YOLOv8n person detector. Standard Ultralytics export layout.
Fetch "https://github.com/Hyuto/yolov8-onnxruntime-web/raw/master/public/model/yolov8n.onnx" "$models/yolov8n.onnx"
# InsightFace SCRFD detector + ArcFace recognizer (ONNX, buffalo_l pack).
Fetch "$HF/immich-app/buffalo_l/resolve/main/detection/model.onnx"   "$models/scrfd_500m.onnx"
Fetch "$HF/immich-app/buffalo_l/resolve/main/recognition/model.onnx" "$models/arcface_r100.onnx"

# Piper engine (Windows amd64 prebuilt) — required for TTS (voices alone are not enough).
Write-Host "Piper engine ->" -ForegroundColor Green
$piperEngine = Join-Path $models 'piper-engine'
$piperExe = Join-Path $piperEngine 'piper.exe'
if (-not (Test-Path $piperExe)) {
  $zip = Join-Path $env:TEMP 'piper_windows_amd64.zip'
  $url = 'https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_windows_amd64.zip'
  Write-Host "  [get ] piper_windows_amd64.zip" -ForegroundColor Cyan
  & curl.exe -L --fail --retry 3 -o $zip $url
  if ($LASTEXITCODE -eq 0 -and (Test-Path $zip)) {
    $extract = Join-Path $env:TEMP 'piper_extract'
    if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
    Expand-Archive -Path $zip -DestinationPath $extract -Force
    $src = Join-Path $extract 'piper'
    if (Test-Path $src) {
      New-Item -ItemType Directory -Force -Path $piperEngine | Out-Null
      Copy-Item -Path (Join-Path $src '*') -Destination $piperEngine -Recurse -Force
      Write-Host "  [ok  ] piper.exe + espeak-ng-data" -ForegroundColor DarkGray
    } else {
      Write-Warning "    piper archive layout unexpected"
    }
  } else {
    Write-Warning "    failed to download piper engine"
  }
} else {
  Write-Host "  [skip] piper.exe" -ForegroundColor DarkGray
}

Write-Host "`nDone. Launch Polymath -- LLMs auto-register on first run (Model Manager to adjust roles)." -ForegroundColor Green
Write-Host "For high-quality neural TTS (recommended):  powershell -File scripts/setup-kokoro.ps1" -ForegroundColor Cyan
Write-Host "For GPU inference (8 GB+ VRAM):              powershell -File scripts/build-gpu.ps1" -ForegroundColor Cyan
if (-not $Heavy) {
  Write-Host "Heavy 27B is parked (use -Heavy on capable machines). See docs/MODELS.md." -ForegroundColor DarkGray
}
