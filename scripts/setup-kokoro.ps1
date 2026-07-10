<#
.SYNOPSIS
  Install Kokoro neural TTS (ONNX) for Polymath — high-quality real-time speech.

.DESCRIPTION
  Creates a local Python venv under data/models/kokoro-engine/, installs
  kokoro-onnx + onnxruntime, downloads the v1.0 model + voices, and writes a
  kokoro_worker.cmd launcher that Polymath's TTS driver will prefer over Piper.

  Target: 8 GB+ VRAM machines. Kokoro runs on CPU (does not steal LLM VRAM).
  ~80–300 MB model; real-time or better on modern CPUs.

.PARAMETER Root  App data root (default: repo data/ or build cpu data if present).
.EXAMPLE  powershell -File scripts/setup-kokoro.ps1
#>
[CmdletBinding()]
param(
  [string]$Root = ""
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (-not $Root) {
  $candidates = @(
    (Join-Path $repo 'build\cpu\bin\Release\data'),
    (Join-Path $repo 'data')
  )
  foreach ($c in $candidates) {
    if (Test-Path $c) { $Root = $c; break }
  }
  if (-not $Root) { $Root = Join-Path $repo 'data' }
}

$engine = Join-Path $Root 'models\kokoro-engine'
$workerSrc = Join-Path $repo 'tools\kokoro_worker\kokoro_worker.py'
New-Item -ItemType Directory -Force -Path $engine | Out-Null

if (-not (Test-Path $workerSrc)) {
  throw "kokoro_worker.py not found at $workerSrc"
}
Copy-Item $workerSrc (Join-Path $engine 'kokoro_worker.py') -Force

# Prefer Python 3.11 (kokoro-onnx is mature there).
$py = $null
foreach ($c in @(
  "$env:LOCALAPPDATA\Programs\Python\Python311\python.exe",
  "C:\Users\Yakub\AppData\Local\Programs\Python\Python311\python.exe",
  (Get-Command python -ErrorAction SilentlyContinue).Source
)) {
  if ($c -and (Test-Path $c)) { $py = $c; break }
}
if (-not $py) { throw "Python 3.11+ not found" }
Write-Host "Using Python: $py" -ForegroundColor Cyan

$venv = Join-Path $engine 'venv'
if (-not (Test-Path (Join-Path $venv 'Scripts\python.exe'))) {
  Write-Host "Creating venv at $venv ..." -ForegroundColor Cyan
  & $py -m venv $venv
  if ($LASTEXITCODE -ne 0) { throw "venv create failed" }
}

$venvPy = Join-Path $venv 'Scripts\python.exe'
Write-Host "Installing kokoro-onnx + onnxruntime ..." -ForegroundColor Cyan
& $venvPy -m pip install --upgrade pip | Out-Null
& $venvPy -m pip install "kokoro-onnx>=0.4.0" "onnxruntime>=1.17" "numpy" "soundfile"
if ($LASTEXITCODE -ne 0) { throw "pip install failed" }

# Model files — official kokoro-onnx release assets (v1.0).
$HF = 'https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.0'
$model = Join-Path $engine 'kokoro-v1.0.onnx'
$voices = Join-Path $engine 'voices-v1.0.bin'

function Fetch($url, $dest) {
  if (Test-Path $dest) {
    Write-Host "  [skip] $(Split-Path $dest -Leaf)" -ForegroundColor DarkGray
    return
  }
  Write-Host "  [get ] $(Split-Path $dest -Leaf)" -ForegroundColor Cyan
  & curl.exe -L --fail --retry 3 -C - -o $dest $url
  if ($LASTEXITCODE -ne 0) { throw "download failed: $url" }
}

Write-Host "Kokoro models ->" -ForegroundColor Green
Fetch "$HF/kokoro-v1.0.onnx" $model
Fetch "$HF/voices-v1.0.bin" $voices

# Default voice: af_sky (natural female). Override with KOKORO_VOICE.
$defaultVoice = if ($env:KOKORO_VOICE) { $env:KOKORO_VOICE } else { 'af_sky' }

# Launcher — .cmd so the C++ side can treat it like piper.exe.
$cmdPath = Join-Path $engine 'kokoro_worker.cmd'
$cmdBody = @"
@echo off
setlocal
set "ENGINE=%~dp0"
set "VOICE=%KOKORO_VOICE%"
if "%VOICE%"=="" set "VOICE=$defaultVoice"
"%ENGINE%venv\Scripts\python.exe" "%ENGINE%kokoro_worker.py" --model "%ENGINE%kokoro-v1.0.onnx" --voices "%ENGINE%voices-v1.0.bin" --voice "%VOICE%" --sample-rate 24000 %*
"@
Set-Content -Path $cmdPath -Value $cmdBody -Encoding ascii

# Voice stub so TtsPiper resolveModel() finds a "voice" folder layout.
# Kokoro uses named voices inside voices.bin; we still present one default
# voice id for the UI/personality path.
$voiceId = "kokoro-$defaultVoice"
$voiceDir = Join-Path $Root "models\kokoro\$voiceId"
New-Item -ItemType Directory -Force -Path $voiceDir | Out-Null
# Minimal JSON config the driver reads for sample_rate.
$config = @{ audio = @{ sample_rate = 24000 }; voice = $defaultVoice } | ConvertTo-Json
Set-Content -Path (Join-Path $voiceDir "$voiceId.onnx.json") -Value $config -Encoding utf8
# Marker file (not a real ONNX — engine path is separate).
if (-not (Test-Path (Join-Path $voiceDir "$voiceId.onnx"))) {
  Set-Content -Path (Join-Path $voiceDir "$voiceId.onnx") -Value "kokoro-stub" -Encoding ascii
}

Write-Host "`nKokoro ready." -ForegroundColor Green
Write-Host "  engine: $cmdPath"
Write-Host "  voice:  $voiceId ($defaultVoice)"
Write-Host "  Polymath will prefer Kokoro over Piper when kokoro_worker.cmd is present."
