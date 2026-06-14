<#
.SYNOPSIS
  The ONE build of Hearth: a single auto-detecting desktop binary.

.DESCRIPTION
  Produces one Hearth.exe that ships ggml's CPU backend (as per-ISA DLLs picked
  at runtime) and, when a CUDA toolkit is available at build time, an additional
  ggml-cuda.dll. At startup the app loads whatever backends sit beside the exe and
  uses the GPU if an NVIDIA device is present, falling back to CPU automatically.
  No more separate build/cpu and build/cuda trees.

  This is enabled by POLYMATH_BACKEND_DL (GGML_BACKEND_DL): ggml builds its
  backends as runtime-loadable libraries instead of compiling one fixed backend
  into the exe. The runtime selection lives in src/inference/vram_budget.cpp,
  which queries ggml's device registry (not cudart) so the binary carries no hard
  CUDA dependency.

  FLAVOR (-Flavor):
    auto  (default) build the CUDA backend when a toolkit is found, else CPU-only
    cuda            require a CUDA toolkit and build ggml-cuda.dll
    cpu             CPU-only (no CUDA backend even if a toolkit exists)

  The CUDA backend needs nvcc, which cannot tolerate a space in any path, so the
  CUDA flavor builds with Ninja through a no-space NTFS junction (default C:\pm),
  exactly like the old build-gpu.ps1. The CPU flavor uses the Visual Studio
  generator (no nvcc, no junction).

.PARAMETER Flavor       auto | cuda | cpu   (default auto)
.PARAMETER CudaToolkit  Portable CUDA root (default build/deps/cuda/toolkit).
.PARAMETER Arch         CUDA arch (default 86 = RTX 30-series).
.PARAMETER QtVersion    Qt version (default 6.6.3).
.PARAMETER Junction     No-space build path for the CUDA flavor (default C:\pm).
.PARAMETER SkipDeploy   Build only; skip windeployqt + runtime DLL copy.

.EXAMPLE  pwsh scripts/build.ps1                  # auto: GPU backend if a toolkit is present
.EXAMPLE  pwsh scripts/build.ps1 -Flavor cpu      # force a CPU-only binary
#>
[CmdletBinding()]
param(
  [ValidateSet('auto','cuda','cpu')] [string]$Flavor = 'auto',
  [string]$CudaToolkit = (Join-Path $PSScriptRoot '..\build\deps\cuda\toolkit'),
  [string]$Arch        = '86',
  [string]$QtVersion   = '6.6.3',
  [string]$OpenCVVersion = '4.9.0',
  [string]$OrtVersion  = '1.17.3',
  [string]$Junction    = 'C:\pm',
  [switch]$SkipDeploy
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$deps = Join-Path $repo 'build\deps'
function Step($m) { Write-Host "==> $m" -ForegroundColor Cyan }

# --- 0. Resolve flavor (does a usable CUDA toolkit exist?) -------------------
$nvcc = $null
if (Test-Path "$CudaToolkit\bin\nvcc.exe") { $nvcc = (Resolve-Path "$CudaToolkit\bin\nvcc.exe").Path }
elseif (Get-Command nvcc -ErrorAction SilentlyContinue) { $nvcc = (Get-Command nvcc).Source }

if ($Flavor -eq 'auto') { $Flavor = if ($nvcc) { 'cuda' } else { 'cpu' } }
if ($Flavor -eq 'cuda' -and -not $nvcc) {
  throw "Flavor 'cuda' requested but no nvcc found (looked in $CudaToolkit\bin and PATH). Assemble the portable toolkit or use -Flavor cpu."
}
Write-Host "Building the single Hearth binary — flavor: $Flavor$(if($nvcc){" (nvcc: $nvcc)"})" -ForegroundColor Green

# --- 1. Prerequisites (Qt / OpenCV / ONNX Runtime / vcpkg small libs) --------
git -C $repo submodule update --init --recursive 2>$null
if (-not (Test-Path "$repo\third_party\sqlite3\sqlite3.c")) {
  throw "third_party/sqlite3/sqlite3.c missing (committed; check your checkout)."
}
New-Item -ItemType Directory -Force $deps | Out-Null

$qtDir = "C:\Qt\$QtVersion\msvc2019_64"
if (-not (Test-Path "$qtDir\lib\cmake\Qt6\Qt6Config.cmake")) {
  Step "installing Qt $QtVersion via aqtinstall"
  python -m pip install -q aqtinstall
  python -m aqt install-qt windows desktop $QtVersion win64_msvc2019_64 -O C:\Qt -m qtmultimedia qtimageformats qtshadertools qthttpserver qtwebsockets
}
$cvCfg = "$deps\opencv\build\x64\vc16\lib"
if (-not (Test-Path "$cvCfg\OpenCVConfig.cmake")) {
  Step "downloading OpenCV $OpenCVVersion"
  $exe = "$deps\opencv.exe"
  Invoke-WebRequest "https://github.com/opencv/opencv/releases/download/$OpenCVVersion/opencv-$OpenCVVersion-windows.exe" -OutFile $exe -UseBasicParsing
  & $exe "-o$deps" -y | Out-Null
}
$ortRoot = "$deps\onnxruntime-win-x64-$OrtVersion"
if (-not (Test-Path "$ortRoot\lib\onnxruntime.lib")) {
  Step "downloading ONNX Runtime $OrtVersion (CPU)"
  $zip = "$deps\ort.zip"
  Invoke-WebRequest "https://github.com/microsoft/onnxruntime/releases/download/v$OrtVersion/onnxruntime-win-x64-$OrtVersion.zip" -OutFile $zip -UseBasicParsing
  Expand-Archive -Force $zip $deps
}
$vcpkg = "$repo\third_party\vcpkg"
if (-not (Test-Path "$vcpkg\vcpkg.exe")) {
  Step "bootstrapping vcpkg"
  if (-not (Test-Path "$vcpkg\.git")) { git clone --depth 1 https://github.com/microsoft/vcpkg $vcpkg }
  & "$vcpkg\bootstrap-vcpkg.bat" -disableMetrics | Out-Null
}
if (-not (Test-Path "$vcpkg\installed\x64-windows\share\spdlog\spdlogConfig.cmake")) {
  Step "vcpkg install small libs (classic mode)"
  Push-Location $env:TEMP
  & "$vcpkg\vcpkg.exe" install sqlite3 nlohmann-json fmt spdlog libsamplerate openssl --triplet x64-windows --vcpkg-root $vcpkg
  Pop-Location
}

$buildDir = "$repo\build\dist"

# --- 2. Configure + build ----------------------------------------------------
if ($Flavor -eq 'cpu') {
  Step "configure (Visual Studio 17 2022, single binary, CPU backend)"
  cmake -S $repo -B $buildDir -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_BUILD_TYPE=Release `
    -DPOLYMATH_BACKEND_DL=ON -DPOLYMATH_USE_CUDA=OFF -DGGML_CUDA=OFF -DPOLYMATH_BUILD_TESTS=OFF `
    -DCMAKE_TOOLCHAIN_FILE="$vcpkg\scripts\buildsystems\vcpkg.cmake" `
    -DVCPKG_MANIFEST_MODE=OFF -DVCPKG_TARGET_TRIPLET=x64-windows `
    -DCMAKE_PREFIX_PATH="$qtDir" -DOpenCV_DIR="$cvCfg" `
    -DPOLYMATH_ONNXRUNTIME_ROOT="$ortRoot"
  Step "build"
  cmake --build $buildDir --config Release --target Hearth -j 6
  $bin = "$buildDir\bin\Release"
}
else {
  # CUDA flavor: nvcc + Ninja + a no-space junction (nvcc rejects spaces in paths).
  $work = $repo
  if ($repo -match ' ') {
    if (-not (Test-Path $Junction)) {
      Step "junction $Junction -> $repo (nvcc cannot handle the space in the repo path)"
      New-Item -ItemType Junction -Path $Junction -Target $repo | Out-Null
    } elseif ((Get-Item $Junction).Target -ne $repo) {
      throw "$Junction already points elsewhere; remove it or pass -Junction <other>."
    }
    $work = $Junction
  }
  $workF = $work -replace '\\','/'
  $cudaF = (Split-Path (Split-Path $nvcc)) -replace '\\','/'   # toolkit root
  $qtF   = ($qtDir -replace '\\','/')
  $ninja = (Get-Command ninja -ErrorAction SilentlyContinue).Source
  if (-not $ninja) { $ninja = "$env:LOCALAPPDATA\Microsoft\WinGet\Links\ninja.exe" }
  if (-not (Test-Path $ninja)) { throw "ninja not found (winget install Ninja-build.Ninja)" }
  $vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
  if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

  $bat = @"
@echo off
call "$vcvars" >nul 2>&1
set "PATH=$cudaF\bin;$(Split-Path $ninja);%PATH%"
cd /d "$work"
cmake -S "$workF" -B "$workF/build/dist" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DPOLYMATH_BACKEND_DL=ON -DPOLYMATH_USE_CUDA=ON -DGGML_CUDA=ON ^
  -DPOLYMATH_BUILD_TESTS=OFF ^
  -DCMAKE_CUDA_COMPILER="$cudaF/bin/nvcc.exe" -DCUDAToolkit_ROOT="$cudaF" ^
  -DCMAKE_CUDA_ARCHITECTURES=$Arch ^
  -DCMAKE_MAKE_PROGRAM="$($ninja -replace '\\','/')" ^
  -DCMAKE_TOOLCHAIN_FILE="$workF/third_party/vcpkg/scripts/buildsystems/vcpkg.cmake" ^
  -DVCPKG_MANIFEST_MODE=OFF -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DCMAKE_PREFIX_PATH="$qtF;$workF/third_party/vcpkg/installed/x64-windows" ^
  -DQt6_DIR="$qtF/lib/cmake/Qt6" ^
  -DOpenCV_DIR="$workF/build/deps/opencv/build/x64/vc16/lib" ^
  -DPOLYMATH_ONNXRUNTIME_ROOT="$workF/build/deps/onnxruntime-win-x64-$OrtVersion" || exit /b 1
cmake --build "$workF/build/dist" --config Release --target Hearth -j 6 || exit /b 1
"@
  $batPath = "$work\build\_dist_build.bat"
  Set-Content -Path $batPath -Value $bat -Encoding ascii
  Step "configure + build (Ninja + nvcc — compiles the CUDA backend, minutes)"
  & cmd /c "`"$batPath`""
  if ($LASTEXITCODE -ne 0) { throw "CUDA build failed ($LASTEXITCODE)" }
  $bin = "$work\build\dist\bin"
}

if (-not (Test-Path "$bin\Hearth.exe")) { throw "build finished but $bin\Hearth.exe is missing" }
Write-Host "Built $bin\Hearth.exe" -ForegroundColor Green
# The ggml/llama/whisper DLLs (incl. ggml-cpu-*.dll and, for cuda, ggml-cuda.dll)
# are emitted straight into the runtime dir beside the exe by CMake.
$ggml = Get-ChildItem "$bin\ggml*.dll","$bin\llama*.dll" -ErrorAction SilentlyContinue
Write-Host ("Backend libraries: " + (($ggml | ForEach-Object Name) -join ', ')) -ForegroundColor DarkGray

if ($SkipDeploy) { return }

# --- 3. Deploy the rest of the runtime --------------------------------------
Step "windeployqt + runtime DLLs"
& "$qtDir\bin\windeployqt.exe" --qmldir "$repo\src\ui\qml" --no-translations "$bin\Hearth.exe" | Out-Null
New-Item -ItemType Directory -Force "$bin\platforms" | Out-Null
Copy-Item "$qtDir\plugins\platforms\qoffscreen.dll" "$bin\platforms\" -Force -ErrorAction SilentlyContinue
Get-ChildItem "$deps\opencv\build\x64\vc16\bin\opencv_world*.dll",
              "$deps\opencv\build\x64\vc16\bin\opencv_videoio_ffmpeg*.dll" -ErrorAction SilentlyContinue |
  Where-Object Name -notmatch 'd\.dll$' | ForEach-Object { Copy-Item $_.FullName $bin -Force }
$vcpkgBin = "$vcpkg\installed\x64-windows\bin"
foreach ($d in 'fmt.dll','spdlog.dll','libcrypto-3-x64.dll','libssl-3-x64.dll') {
  if (Test-Path "$vcpkgBin\$d") { Copy-Item "$vcpkgBin\$d" $bin -Force }
}
if ($Flavor -eq 'cuda') {
  Step "copying CUDA runtime DLLs (cudart / cublas / cublasLt)"
  $cudaRoot = Split-Path (Split-Path $nvcc)
  $cudaDll = Get-ChildItem "$cudaRoot\bin\x64\*.dll","$cudaRoot\bin\*.dll" -ErrorAction SilentlyContinue |
             Where-Object Name -match 'cudart64|cublas64|cublasLt64'
  $cudaDll | ForEach-Object { Copy-Item $_.FullName $bin -Force }
}

Write-Host "`nSingle binary deployed -> $bin\Hearth.exe ($Flavor)" -ForegroundColor Green
Write-Host "It auto-detects an NVIDIA GPU at runtime and falls back to CPU. Models: pwsh scripts/fetch-models.ps1" -ForegroundColor DarkGray
