<#
.SYNOPSIS
  Hearth first-run wizard — guides a fresh user from "just unzipped / just
  installed" to a working assistant: GPU/driver check, guided model download
  (minimal vs full), then launch. Never drops the user into a dead, model-less app.

.DESCRIPTION
  Hearth loads its LLM/voice/vision models from data\models\ at runtime and
  self-disables any feature whose model is absent. On a clean box that folder is
  empty, so the very first thing a user needs is models. This wizard:

    1. Locates the app + its data\models\ folder (portable bundle layout, or an
       installed layout under %LOCALAPPDATA%\Hearth).
    2. Detects whether models are already present — if so, it just offers to
       launch (idempotent; safe to re-run).
    3. Runs the GPU/driver check (scripts\check-gpu.ps1) and tells the user
       whether the GPU build will accelerate inference or it'll run on CPU.
    4. Offers a model set:
         [1] Minimal  (~3.5 GB) — Fast LLM + embeddings + whisper + voices +
                       vision detectors. Chat, voice, memory, cameras work.
         [2] Full     (~28 GB)  — adds the 27B Heavy deep-work model and the
                       Gemma 3 4B Vision VLM (image understanding).
         [3] Skip     — bring your own GGUF/ONNX (prints the layout) or do it
                       later from the in-app Model Manager.
       …then drives scripts\fetch-models.ps1 with the matching flags.
    5. Launches Hearth.exe.

  This wizard lives entirely in scripts\ and calls fetch-models.ps1 directly, so
  it needs no new backend invokable. (A future in-app wizard could call a backend
  fetchModels()/openModelsFolder() — see docs\sessions\contract-requests.md.)

.PARAMETER AppDir     Folder containing Hearth.exe (default: the script's parent
                      — works for both the repo's build tree and a bundle where
                      this script sits beside the exe via package.ps1).
.PARAMETER DataDir    App data root holding models\ (default: <AppDir>\data).
.PARAMETER NonInteractive  Don't prompt; use -Choice for the model set and don't
                      launch. For CI / scripted validation.
.PARAMETER Choice     'minimal' | 'full' | 'skip' — preselect in -NonInteractive.
.PARAMETER NoLaunch   Do everything but launching the app.

.EXAMPLE  pwsh scripts\first-run.ps1
.EXAMPLE  pwsh scripts\first-run.ps1 -NonInteractive -Choice skip -NoLaunch
#>
[CmdletBinding()]
param(
  [string]$AppDir,
  [string]$DataDir,
  [switch]$NonInteractive,
  [ValidateSet('minimal','full','skip')] [string]$Choice = 'minimal',
  [switch]$NoLaunch
)
$ErrorActionPreference = 'Stop'

function Title($t) { Write-Host "`n== $t ==" -ForegroundColor Cyan }
function Info($t)  { Write-Host "  $t" }
function Good($t)  { Write-Host "  $t" -ForegroundColor Green }
function Warn($t)  { Write-Host "  $t" -ForegroundColor Yellow }

# --- Resolve where the app + its data live -----------------------------------
# Default AppDir = the dir holding this script (bundle layout puts first-run.ps1
# beside Hearth.exe). In the repo, fall back to the CPU build's bin dir.
if (-not $AppDir) {
  if (Test-Path (Join-Path $PSScriptRoot 'Hearth.exe')) {
    $AppDir = $PSScriptRoot
  } else {
    $repo = Resolve-Path (Join-Path $PSScriptRoot '..')
    foreach ($c in "$repo\build\cuda\bin", "$repo\build\cpu\bin\Release") {
      if (Test-Path (Join-Path $c 'Hearth.exe')) { $AppDir = $c; break }
    }
    if (-not $AppDir) { $AppDir = "$repo\build\cpu\bin\Release" }  # best-effort
  }
}
$exe = Join-Path $AppDir 'Hearth.exe'
if (-not $DataDir) { $DataDir = Join-Path $AppDir 'data' }
$modelsDir = Join-Path $DataDir 'models'

$fetch = Join-Path $PSScriptRoot 'fetch-models.ps1'
$gpuChk = Join-Path $PSScriptRoot 'check-gpu.ps1'

Write-Host @"

  ____       _                       _   _
 |  _ \ ___ | |_   _ _ __ ___   __ _| |_| |__
 | |_) / _ \| | | | | '_ ` _ \ / _` | __| '_ \
 |  __/ (_) | | |_| | | | | | | (_| | |_| | | |
 |_|   \___/|_|\__, |_| |_| |_|\__,_|\__|_| |_|   first-run setup
               |___/
"@ -ForegroundColor Magenta

Info "App:    $exe"
Info "Models: $modelsDir"
if (-not (Test-Path $exe)) {
  Warn "Hearth.exe not found at $AppDir — pass -AppDir <folder with Hearth.exe>."
}

# --- 1. Already have models? --------------------------------------------------
function Test-HasModels($dir) {
  if (-not (Test-Path $dir)) { return $false }
  # A usable install needs at least one Fast LLM GGUF (chat/agent can't run without it).
  $gguf = Get-ChildItem (Join-Path $dir 'llm') -Filter *.gguf -ErrorAction SilentlyContinue
  return [bool]$gguf
}

if (Test-HasModels $modelsDir) {
  Title "Models already present"
  Good "Found at least one Fast LLM in $modelsDir — setup looks complete."
  if (-not $NoLaunch -and -not $NonInteractive -and (Test-Path $exe)) {
    $go = Read-Host "  Launch Hearth now? [Y/n]"
    if ($go -notmatch '^[nN]') { Start-Process $exe -WorkingDirectory $AppDir }
  }
  return
}

# --- 2. GPU / driver check ----------------------------------------------------
$gpu = $null
if (Test-Path $gpuChk) {
  try { $gpu = & $gpuChk } catch { Warn "GPU check failed: $($_.Exception.Message)" }
} else {
  Warn "check-gpu.ps1 not found beside this script — skipping GPU detection."
}

# --- 3. Choose a model set ----------------------------------------------------
Title "Download models"
Write-Host @"
  Hearth needs local models to think, hear, speak and see. Pick a set:

   [1] Minimal  (~3.5 GB)  Fast LLM (Gemma 3n E4B) + embeddings + whisper ASR +
                           Piper voices + VAD/wake-word + vision detectors.
                           -> Chat, voice, memory and camera person-detection work.
   [2] Full     (~28 GB)   Everything in Minimal, plus the 27B Heavy deep-work
                           model and the Gemma 3 4B Vision VLM (image Q&A).
   [3] Skip                Bring your own GGUF/ONNX, or do it later in the
                           in-app Model Manager. (Prints the expected layout.)
"@

$sel = $Choice
if (-not $NonInteractive) {
  $ans = Read-Host "  Choice [1=minimal / 2=full / 3=skip] (default 1)"
  switch ($ans.Trim()) {
    '2' { $sel = 'full' }
    '3' { $sel = 'skip' }
    default { $sel = 'minimal' }
  }
}
Info "Selected: $sel"

switch ($sel) {
  'skip' {
    Title "Bring your own models"
    Write-Host @"
  Drop files into this layout under  $modelsDir  (the app auto-registers GGUFs
  on launch; assign roles in the Model Manager):

    llm\        <fast>.gguf          (required — the resident chat/agent model)
                <heavy>.gguf         (optional — on-demand deep-work)
    vlm\        <vision>.gguf + mmproj-*.gguf   (optional — image understanding)
    embeddings\ <embed>.gguf         (memory / semantic search)
    whisper\    ggml-base.en.bin     ggml-tiny.en.bin     (speech-to-text)
    piper\<voice>\<voice>.onnx(.json)                     (text-to-speech)
    vad\        silero_vad.onnx
    wakeword\   melspectrogram.onnx  embedding_model.onnx  <wake>.onnx
    yolov8n.onnx  scrfd_500m.onnx  arcface_r100.onnx       (person + face)

  You can always run this wizard again, or  scripts\fetch-models.ps1  directly.
"@
    Warn "No Fast LLM yet -> chat/voice/agent stay disabled until you add one."
  }
  default {
    if (-not (Test-Path $fetch)) { throw "fetch-models.ps1 not found at $fetch" }
    $minimal = ($sel -eq 'minimal')
    Title ("Fetching the {0} set into {1}" -f $sel, $modelsDir)
    if ($minimal) {
      & $fetch -Root $DataDir -Minimal
    } else {
      & $fetch -Root $DataDir
    }
    if (Test-HasModels $modelsDir) { Good "Models in place." }
    else { Warn "No Fast LLM landed (downloads may have failed). Re-run, or check your connection / HF availability." }
  }
}

# --- 4. Launch ----------------------------------------------------------------
if ($NoLaunch -or $NonInteractive) { Info "Done (no launch)."; return }
if (-not (Test-Path $exe)) { Warn "Skipping launch — Hearth.exe missing."; return }

Title "Launch"
if (-not (Test-HasModels $modelsDir)) {
  Warn "Launching without a Fast model: the app will start but show the cold-start banner and the Model Manager guide until you add one."
}
$go = Read-Host "  Launch Hearth now? [Y/n]"
if ($go -notmatch '^[nN]') { Start-Process $exe -WorkingDirectory $AppDir; Good "Started." }
else { Info "Launch it any time with Run-Hearth.cmd (or Hearth.exe)." }
