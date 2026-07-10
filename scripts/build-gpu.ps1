<#
.SYNOPSIS
  CUDA (GPU) build of Polymath, using a PORTABLE CUDA toolkit (no admin install).

.DESCRIPTION
  Builds llama.cpp / whisper.cpp ggml-CUDA kernels for the GPU and links them into
  Polymath.exe, then deploys the Qt + CUDA + OpenCV runtime next to the exe.

  Assumes the CPU prerequisites already exist (run build-cpu.ps1 once): Qt, OpenCV,
  ONNX Runtime, the small vcpkg libs, and the portable CUDA toolkit assembled under
  build/deps/cuda/toolkit. Uses the Ninja generator + command-line nvcc so the
  admin-only Visual Studio CUDA integration is NOT required.

  Three gotchas this script handles (each cost real debugging time):

  1. SPACE IN THE REPO PATH. nvcc aborts with "A single input file is required for a
     non-link phase..." when any path it sees contains a space (our repo lives in
     "...\Home Assistant"). Fix: build through a no-space NTFS junction (default
     C:\pm -> repo), created here without admin rights. All CMake/Ninja/nvcc paths
     use the junction.

  2. Qt6_DIR + the explicit vcpkg "installed" dir must BOTH be on CMAKE_PREFIX_PATH.
     The vcpkg toolchain does not add them itself under the Ninja generator here, so
     find_package(Qt6) / the small libs fail without them.

  3. MSVC "/flag" leaking onto the nvcc command line. Handled in the top CMakeLists
     (add_compile_options gated by $<COMPILE_LANGUAGE> so /utf-8 /MP /EHsc
     /Zc:char8_t- reach C/C++ but never CUDA). Nothing to do here, noted for context.

.PARAMETER CudaToolkit  Portable CUDA root (default: build/deps/cuda/toolkit).
.PARAMETER Arch         CUDA arch (default 86 = RTX 30-series / 3080 Ti).
.PARAMETER QtDir        Qt msvc kit root (default C:\Qt\6.6.3\msvc2019_64).
.PARAMETER Junction     No-space working path for the build (default C:\pm). Only
                        used when the repo path itself contains a space.
.PARAMETER SkipDeploy   Build only; don't run windeployqt / copy runtime DLLs.

.EXAMPLE  pwsh scripts/build-gpu.ps1
#>
[CmdletBinding()]
param(
  [string]$CudaToolkit = (Join-Path $PSScriptRoot '..\build\deps\cuda\toolkit'),
  [string]$Arch        = '86',
  [string]$QtDir       = 'C:\Qt\6.6.3\msvc2019_64',
  [string]$Junction    = 'C:\pm',
  [switch]$SkipDeploy
)
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

# --- 1. No-space working directory (junction) -------------------------------
# nvcc cannot handle a space anywhere in its paths; build through a junction when
# the repo path contains one. New-Item -ItemType Junction needs no admin rights.
if ($repo -match ' ') {
  if (-not (Test-Path $Junction)) {
    Write-Host "Creating junction $Junction -> $repo" -ForegroundColor Cyan
    New-Item -ItemType Junction -Path $Junction -Target $repo | Out-Null
  } else {
    $tgt = (Get-Item $Junction).Target
    if ($tgt -and ($tgt -ne $repo)) {
      throw "$Junction already exists and points to '$tgt', not '$repo'. Remove it or pass -Junction <other>."
    }
  }
  $work = $Junction
} else {
  $work = $repo            # already space-free; build in place
}

# CMake wants forward slashes; build a /-style root for the -D arguments.
$workF = $work -replace '\\','/'
$deps  = "$work\build\deps"
$cuda  = (Resolve-Path $CudaToolkit).Path
$cudaF = ($cuda -replace '\\','/')
if (-not (Test-Path "$cuda\bin\nvcc.exe")) { throw "nvcc not found at $cuda\bin (assemble the portable toolkit first)" }

$ninja = (Get-Command ninja -ErrorAction SilentlyContinue).Source
if (-not $ninja) { $ninja = "$env:LOCALAPPDATA\Microsoft\WinGet\Links\ninja.exe" }
if (-not (Test-Path $ninja)) { throw "ninja not found (install: winget install Ninja-build.Ninja)" }
$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

$qtF   = ($QtDir -replace '\\','/')
$bin   = "$work\build\cuda\bin"

# --- 2. Configure + build (inside a VS dev env that has nvcc + ninja on PATH) -
# Note: Qt6_DIR and the vcpkg "installed" dir are BOTH required on CMAKE_PREFIX_PATH
# (see gotcha #2). project() declares LANGUAGES C CXX so Ninja has C for sqlite3/ggml.
$bat = @"
@echo off
call "$vcvars" >nul 2>&1
set "PATH=$cuda\bin;$(Split-Path $ninja);%PATH%"
cd /d "$work"
cmake -S "$workF" -B "$workF/build/cuda" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DPOLYMATH_USE_CUDA=ON -DGGML_CUDA=ON ^
  -DCMAKE_CUDA_COMPILER="$cudaF/bin/nvcc.exe" ^
  -DCUDAToolkit_ROOT="$cudaF" ^
  -DCMAKE_CUDA_ARCHITECTURES=$Arch ^
  -DCMAKE_MAKE_PROGRAM="$($ninja -replace '\\','/')" ^
  -DCMAKE_TOOLCHAIN_FILE="$workF/third_party/vcpkg/scripts/buildsystems/vcpkg.cmake" ^
  -DVCPKG_MANIFEST_MODE=OFF -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DCMAKE_PREFIX_PATH="$qtF;$workF/third_party/vcpkg/installed/x64-windows" ^
  -DQt6_DIR="$qtF/lib/cmake/Qt6" ^
  -DOpenCV_DIR="$workF/build/deps/opencv/build/x64/vc16/lib" ^
  -DPOLYMATH_ONNXRUNTIME_ROOT="$workF/build/deps/onnxruntime-win-x64-1.17.3" || exit /b 1
cmake --build "$workF/build/cuda" --config Release -j 6 || exit /b 1
"@
$batPath = "$work\build\_gpu_build.bat"
Set-Content -Path $batPath -Value $bat -Encoding ascii
Write-Host "Configuring + building (this compiles the CUDA kernels — minutes)..." -ForegroundColor Cyan
& cmd /c "`"$batPath`""
if ($LASTEXITCODE -ne 0) { throw "GPU build failed ($LASTEXITCODE) — see Ninja output above" }
if (-not (Test-Path "$bin\Polymath.exe")) { throw "build reported success but $bin\Polymath.exe is missing" }
Write-Host "Built $bin\Polymath.exe" -ForegroundColor Green

if ($SkipDeploy) { return }

# --- 3. Deploy the runtime next to the exe ----------------------------------
# Qt runtime (DLLs + QML) via windeployqt, plus the offscreen platform plugin so
# the app can run headless for verification.
Write-Host "Deploying Qt runtime..." -ForegroundColor Cyan
& "$QtDir\bin\windeployqt.exe" --qmldir "$work\src\ui\qml" --no-translations "$bin\Polymath.exe" | Out-Null
New-Item -ItemType Directory -Force "$bin\platforms" | Out-Null
Copy-Item "$QtDir\plugins\platforms\qoffscreen.dll" "$bin\platforms\" -Force -ErrorAction SilentlyContinue

# CUDA runtime DLLs. The portable toolkit keeps the redistributable DLLs under
# bin\x64 (cudart/cublas/cublasLt); fall back to bin\ for layouts that flatten them.
Write-Host "Copying CUDA + OpenCV + vcpkg runtime DLLs..." -ForegroundColor Cyan
$cudaDll = Get-ChildItem "$cuda\bin\x64\*.dll","$cuda\bin\*.dll" -ErrorAction SilentlyContinue |
           Where-Object Name -match 'cudart64|cublas64|cublasLt64'
$cudaDll | ForEach-Object { Copy-Item $_.FullName $bin -Force }
# OpenCV — the RELEASE world DLL (skip *d.dll debug) AND the FFmpeg video backend
# (opencv_videoio_ffmpeg*_64.dll), which VisionService needs for MJPEG/RTSP cameras.
Get-ChildItem "$deps\opencv\build\x64\vc16\bin\opencv_world*.dll",
              "$deps\opencv\build\x64\vc16\bin\opencv_videoio_ffmpeg*.dll" -ErrorAction SilentlyContinue |
  Where-Object Name -notmatch 'd\.dll$' | ForEach-Object { Copy-Item $_.FullName $bin -Force }
# vcpkg runtime DLLs that Polymath links directly (windeployqt does NOT cover these;
# without them the loader fails before main(): the app hangs on a missing-DLL dialog).
$vcpkgBin = "$work\third_party\vcpkg\installed\x64-windows\bin"
# libcrypto-3-x64.dll: OpenSSL crypto backend for the SQLCipher codec (at-rest
# encryption). Required at runtime; without it the loader fails before main().
foreach ($d in 'fmt.dll','spdlog.dll','libcrypto-3-x64.dll') {
  if (Test-Path "$vcpkgBin\$d") { Copy-Item "$vcpkgBin\$d" $bin -Force }
}

# --- 4. Models. The app reads <exe>\data\models. Models are ~28 GB, so junction
# the CPU build's data dir rather than copy (no admin needed for a junction).
$cpuData = "$work\build\cpu\bin\Release\data"
if ((Test-Path $cpuData) -and -not (Test-Path "$bin\data")) {
  Write-Host "Linking models: $bin\data -> $cpuData" -ForegroundColor Cyan
  New-Item -ItemType Junction -Path "$bin\data" -Target $cpuData | Out-Null
}

Write-Host "`nGPU build deployed -> $bin\Polymath.exe" -ForegroundColor Green
Write-Host "Verify headless:  `$env:QT_QPA_PLATFORM='offscreen'; & '$bin\Polymath.exe'" -ForegroundColor DarkGray
Write-Host "Then check data\logs\polymath.log for 'VramBudget ... CUDA=true'." -ForegroundColor DarkGray
