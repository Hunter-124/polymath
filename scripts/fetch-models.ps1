<#
.SYNOPSIS
  Downloads the default Polymath model set into data/models/.

.DESCRIPTION
  Pulls the GGUF LLMs, whisper ASR model, Piper TTS voices, and the ONNX models
  (wake word, VAD, person detection, face recognition, embeddings) into the
  canonical layout documented in docs/MODELS.md. Everything is local and free.

  All model roles are reconfigurable in the in-app Model Manager, so you can
  point at different files later — this just bootstraps sensible defaults.

.PARAMETER Root
  App data root (defaults to .\data next to the repo, matching the portable build).

.PARAMETER Minimal
  Skip the large/optional downloads (heavy LLM, vision VLM).

.EXAMPLE
  pwsh scripts/fetch-models.ps1 -Minimal
#>
[CmdletBinding()]
param(
  [string]$Root = (Join-Path $PSScriptRoot '..\data'),
  [switch]$Minimal
)

$ErrorActionPreference = 'Stop'
$models = Join-Path $Root 'models'

# Canonical sub-layout (see docs/MODELS.md). The C++ side + Model Manager read
# from here; keep these names if you want zero-config first run.
$dirs = @{
  llm       = Join-Path $models 'llm'
  embed     = Join-Path $models 'embeddings'
  whisper   = Join-Path $models 'whisper'
  piper     = Join-Path $models 'piper'
  wakeword  = Join-Path $models 'onnx\wakeword'
  vad       = Join-Path $models 'onnx\vad'
  yolo      = Join-Path $models 'onnx\yolo'
  face      = Join-Path $models 'onnx\face'
  vlm       = Join-Path $models 'vlm'
}
$dirs.Values | ForEach-Object { New-Item -ItemType Directory -Force -Path $_ | Out-Null }

# Download manifest: each entry = @{ url; dest; optional }.
# NOTE: URLs point at the canonical Hugging Face / GitHub release assets. If a
# repo has moved, update the url — the destination layout is what matters.
$HF = 'https://huggingface.co'
$manifest = @(
  # --- Fast LLM (resident, real-time) -------------------------------------
  @{ url = "$HF/bartowski/Qwen2.5-7B-Instruct-GGUF/resolve/main/Qwen2.5-7B-Instruct-Q4_K_M.gguf";
     dest = Join-Path $dirs.llm 'Qwen2.5-7B-Instruct-Q4_K_M.gguf'; optional = $false }

  # --- Embeddings (memory / RAG) ------------------------------------------
  @{ url = "$HF/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/nomic-embed-text-v1.5.Q4_K_M.gguf";
     dest = Join-Path $dirs.embed 'nomic-embed-text-v1.5.Q4_K_M.gguf'; optional = $false }

  # --- Whisper ASR --------------------------------------------------------
  @{ url = "$HF/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin";
     dest = Join-Path $dirs.whisper 'ggml-base.en.bin'; optional = $false }
  @{ url = "$HF/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin";
     dest = Join-Path $dirs.whisper 'ggml-tiny.en.bin'; optional = $false }  # ambient

  # --- Piper TTS voices (per-personality) ---------------------------------
  @{ url = "$HF/rhasspy/piper-voices/resolve/main/en/en_GB/alan/medium/en_GB-alan-medium.onnx";
     dest = Join-Path $dirs.piper 'en_GB-alan-medium.onnx'; optional = $false }
  @{ url = "$HF/rhasspy/piper-voices/resolve/main/en/en_GB/alan/medium/en_GB-alan-medium.onnx.json";
     dest = Join-Path $dirs.piper 'en_GB-alan-medium.onnx.json'; optional = $false }
  @{ url = "$HF/rhasspy/piper-voices/resolve/main/en/en_US/amy/medium/en_US-amy-medium.onnx";
     dest = Join-Path $dirs.piper 'en_US-amy-medium.onnx'; optional = $false }
  @{ url = "$HF/rhasspy/piper-voices/resolve/main/en/en_US/amy/medium/en_US-amy-medium.onnx.json";
     dest = Join-Path $dirs.piper 'en_US-amy-medium.onnx.json'; optional = $false }

  # --- Silero VAD ----------------------------------------------------------
  @{ url = 'https://github.com/snakers4/silero-vad/raw/master/src/silero_vad/data/silero_vad.onnx';
     dest = Join-Path $dirs.vad 'silero_vad.onnx'; optional = $false }

  # --- Person detection (YOLO, ONNX) --------------------------------------
  @{ url = "$HF/Xenova/yolov8n/resolve/main/onnx/model.onnx";
     dest = Join-Path $dirs.yolo 'yolov8n.onnx'; optional = $false }

  # --- Heavy LLM (on-demand deep work) — large, optional ------------------
  @{ url = "$HF/bartowski/Qwen2.5-32B-Instruct-GGUF/resolve/main/Qwen2.5-32B-Instruct-Q4_K_M.gguf";
     dest = Join-Path $dirs.llm 'Qwen2.5-32B-Instruct-Q4_K_M.gguf'; optional = $true }

  # --- Vision VLM (image analysis / find-object) — large, optional --------
  @{ url = "$HF/ggml-org/Qwen2-VL-7B-Instruct-GGUF/resolve/main/Qwen2-VL-7B-Instruct-Q4_K_M.gguf";
     dest = Join-Path $dirs.vlm 'Qwen2-VL-7B-Instruct-Q4_K_M.gguf'; optional = $true }
  @{ url = "$HF/ggml-org/Qwen2-VL-7B-Instruct-GGUF/resolve/main/mmproj-Qwen2-VL-7B-Instruct-f16.gguf";
     dest = Join-Path $dirs.vlm 'mmproj-Qwen2-VL-7B-Instruct-f16.gguf'; optional = $true }
)

function Get-Model($item) {
  if (Test-Path $item.dest) { Write-Host "  [skip] $(Split-Path $item.dest -Leaf) (exists)" -ForegroundColor DarkGray; return }
  Write-Host "  [get ] $(Split-Path $item.dest -Leaf)" -ForegroundColor Cyan
  try {
    Invoke-WebRequest -Uri $item.url -OutFile $item.dest -UseBasicParsing
  } catch {
    Write-Warning "    failed: $($item.url)`n    $($_.Exception.Message)"
    Write-Warning "    (download manually into $($item.dest) — the URL may have moved)"
  }
}

Write-Host "Polymath model fetch -> $models" -ForegroundColor Green
Write-Host "openWakeWord + face-recognition (SCRFD/ArcFace) ONNX models:" -ForegroundColor Yellow
Write-Host "  these ship as multi-file bundles; see docs/MODELS.md for the exact files and links." -ForegroundColor Yellow

foreach ($m in $manifest) {
  if ($Minimal -and $m.optional) { continue }
  Get-Model $m
}

Write-Host "`nDone. Review docs/MODELS.md, then launch Polymath and assign roles in the Model Manager." -ForegroundColor Green
