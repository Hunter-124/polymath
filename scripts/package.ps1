<#
.SYNOPSIS
  Assemble a portable Polymath bundle (Polymath.exe + every runtime DLL it needs)
  and pack it into a single distributable .zip — the project's honest "packed binary"
  (one primary exe + runtime DLLs + a models/ folder; see docs/PACKAGING.md).

.DESCRIPTION
  Takes an already-built + deployed tree (run scripts/build-gpu.ps1 or build-cpu.ps1
  first) and stages a clean, self-contained folder:
    - Polymath.exe (drops the llama-*.exe dev tools and all .lib/.exp/.pdb)
    - the Qt runtime windeployqt placed beside it (Qt6*.dll, platforms\, qml\, ...)
    - the engine DLLs (ggml*, llama*, mtmd, whisper), onnxruntime, OpenCV, fmt/spdlog
    - the CUDA runtime DLLs (cudart/cublas/cublasLt) for the GPU flavour
    - the VC++ 2015-2022 redistributable DLLs (so it runs on a clean machine)
  then zips it to dist\Polymath-<ver>-win64-<flavour>.zip.

  Models (~28 GB) are NOT bundled by default — the bundle ships fetch-models.ps1 and
  an empty data\models\ instead. Pass -IncludeModels for a fully self-contained (huge)
  zip.

.PARAMETER Flavor        cuda (GPU, default) or cpu.
.PARAMETER IncludeModels Bundle the resolved data\models\ tree (~28 GB). Off by default.
.PARAMETER NoZip         Stage the folder but skip creating the .zip.

.EXAMPLE  pwsh scripts/package.ps1                 # GPU app bundle -> dist\*.zip
.EXAMPLE  pwsh scripts/package.ps1 -Flavor cpu
.EXAMPLE  pwsh scripts/package.ps1 -IncludeModels  # self-contained, ~28 GB
#>
[CmdletBinding()]
param(
  [ValidateSet('cuda','cpu')] [string]$Flavor = 'cuda',
  [string]$OutRoot = '',
  [switch]$IncludeModels,
  [switch]$NoZip
)
$ErrorActionPreference = 'Stop'
# $PSScriptRoot can be empty under Windows PowerShell 5.1 when invoked via
# `powershell -File` from some hosts — resolve the script directory ourselves.
$scriptDir = $PSScriptRoot
if (-not $scriptDir) {
  $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
}
if (-not $scriptDir) {
  $scriptDir = Split-Path -Parent $PSCommandPath
}
if (-not $OutRoot) {
  $OutRoot = Join-Path $scriptDir '..\dist'
}
$repo = (Resolve-Path (Join-Path $scriptDir '..')).Path

# Version from project(Polymath VERSION x.y.z)
$ver = '0.0.0'
if ((Get-Content "$repo\CMakeLists.txt" -Raw) -match 'project\(Polymath\s+VERSION\s+([0-9.]+)') { $ver = $Matches[1] }

$bin = if ($Flavor -eq 'cuda') { "$repo\build\cuda\bin" } else { "$repo\build\cpu\bin\Release" }
if (-not (Test-Path "$bin\Polymath.exe")) {
  throw "Polymath.exe not found in $bin — build it first (pwsh scripts/build-$Flavor.ps1)."
}

$name  = "Polymath-$ver-win64-$Flavor"
$stage = Join-Path $OutRoot $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
Write-Host "Staging $name ..." -ForegroundColor Cyan

# 1) Files: keep Polymath.exe + all DLLs; drop the llama-*.exe dev tools and build cruft.
#    Also drop test/headless run logs that ctest + offscreen verify runs leave in the
#    build dir (headless.*.log, run_*.txt) — they are not runtime files and have no
#    business in a shipped bundle/installer.
Get-ChildItem $bin -File | Where-Object {
  $_.Name -notmatch '\.(lib|exp|pdb|ilk|manifest|log)$' -and
  $_.Name -notmatch '^(headless\.|run_).*\.(txt|log)$' -and
  -not ($_.Extension -eq '.exe' -and $_.Name -ne 'Polymath.exe')
} | ForEach-Object { Copy-Item $_.FullName $stage -Force }

# 2) Subdirectories windeployqt created (platforms\, qml\, imageformats\, ...). Never
#    the data\ junction (that is the ~28 GB models tree).
Get-ChildItem $bin -Directory | Where-Object Name -ne 'data' |
  ForEach-Object { Copy-Item $_.FullName $stage -Recurse -Force }

# 3) VC++ runtime so the zip runs on a machine without the redist installed.
foreach ($d in 'msvcp140.dll','msvcp140_1.dll','vcruntime140.dll','vcruntime140_1.dll','concrt140.dll') {
  $p = Join-Path $env:WINDIR "System32\$d"
  if (Test-Path $p) { Copy-Item $p $stage -Force }
}

# 4) Models. ~28 GB — opt-in. Otherwise ship the fetcher + an empty models\ dir.
New-Item -ItemType Directory -Force "$stage\data\models" | Out-Null
if ($IncludeModels) {
  Write-Host "Copying models (this is large)..." -ForegroundColor Yellow
  if (Test-Path "$bin\data\models") { Copy-Item "$bin\data\models\*" "$stage\data\models\" -Recurse -Force }
} else {
  # Ship the model fetcher + the first-run wizard + GPU check so a cold-started
  # user is guided rather than dropped into a model-less app.
  foreach ($s in 'fetch-models.ps1','first-run.ps1','check-gpu.ps1') {
    Copy-Item "$repo\scripts\$s" "$stage\$s" -Force
  }
  Set-Content "$stage\data\models\PUT-MODELS-HERE.txt" @"
This folder holds Polymath's local models (GGUF LLMs + ONNX perception/voice).
They are not bundled (the default set is ~28 GB). The easy path — from the bundle
root run the first-run wizard (GPU check + guided download):

    pwsh .\first-run.ps1

or fetch directly (use -Minimal for the ~3.5 GB starter set):

    pwsh .\fetch-models.ps1 -Root .\data -Minimal

or copy your own models into the layout under data\models\ (see fetch-models.ps1
header for the expected paths). The app self-disables any feature whose model is absent.
"@
}

# 5) Convenience launcher + top-level README.
# Launcher: on first run (no Fast LLM under data\models\llm) drive the wizard;
# otherwise launch the app directly. Models bundled (-IncludeModels) -> always direct.
if ($IncludeModels) {
  Set-Content "$stage\Run-Polymath.cmd" "@echo off`r`ncd /d `"%~dp0`"`r`nstart `"`" `"Polymath.exe`"`r`n"
} else {
  # Prefer PowerShell 7 (pwsh) for nicer output, but fall back to the in-box
  # Windows PowerShell 5.1 (powershell.exe): a clean Windows box has 5.1 but not
  # pwsh. The first-run scripts are saved UTF-8-with-BOM so 5.1 parses them too.
  $launch = @'
@echo off
cd /d "%~dp0"
if exist "data\models\llm\*.gguf" goto run
echo No models found - launching first-run setup...
where pwsh >nul 2>&1
if %ERRORLEVEL%==0 (
  pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0first-run.ps1"
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0first-run.ps1"
)
goto end
:run
start "" "Polymath.exe"
:end
'@
  Set-Content "$stage\Run-Polymath.cmd" ($launch -replace "`n","`r`n")
}
$gpuNote = if ($Flavor -eq 'cuda') {
  "This is the CUDA (GPU) build: it needs an NVIDIA GPU + a recent driver. llama/whisper`r`nrun on the GPU; the ONNX perception models (YOLO/face) run on CPU."
} else { "This is the CPU build (no GPU required)." }
Set-Content "$stage\README.txt" @"
Polymath $ver — portable Windows bundle ($Flavor)

FIRST RUN:  double-click Run-Polymath.cmd.
  If no models are present yet it launches the first-run wizard (first-run.ps1):
  a GPU/driver check, then a guided model download (Minimal ~3.5 GB or Full ~28 GB),
  then it starts the app. Once models exist, Run-Polymath.cmd just launches directly.
$gpuNote

Models live in data\models\ and are NOT bundled (the default set is ~28 GB). To
populate them yourself instead of via the wizard:
    pwsh .\first-run.ps1                      (guided)
    pwsh .\fetch-models.ps1 -Root .\data -Minimal   (just the starter set)
    or drop your own GGUF/ONNX into data\models\ (see fetch-models.ps1 header).
The app self-disables any feature whose model is absent — it never silently dies.

Logs: data\logs\polymath.log.  Everything here is self-contained except the models.
"@

$sizeMB = [math]::Round((Get-ChildItem $stage -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)
Write-Host "Staged $name : $sizeMB MB ($((Get-ChildItem $stage -Recurse -File).Count) files)" -ForegroundColor Green

if ($NoZip) { Write-Host "Folder: $stage" -ForegroundColor Green; return }

# 6) Zip. Prefer 7-Zip (fast); fall back to Compress-Archive.
$zip = Join-Path $OutRoot "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
$sevenZip = 'C:\Program Files\7-Zip\7z.exe'
Write-Host "Packing $zip ..." -ForegroundColor Cyan
# Archive the staged FOLDER (not its contents) so the zip extracts to a single
# top-level "$name\" directory instead of splattering files into the cwd.
if (Test-Path $sevenZip) {
  Push-Location $OutRoot
  try { & $sevenZip a -tzip -mx=5 -bso0 -bsp0 "$zip" "$name" | Out-Null }
  finally { Pop-Location }
  if ($LASTEXITCODE -ne 0) { throw "7-Zip failed ($LASTEXITCODE)" }
} else {
  Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
}
$zipMB = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "`nPacked -> $zip  ($zipMB MB)" -ForegroundColor Green
