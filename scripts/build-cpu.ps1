<#
.SYNOPSIS
  One-command CPU build of Polymath (the recipe verified during bring-up).

.DESCRIPTION
  Idempotent. Fetches the prebuilt dependencies if missing, builds the small
  libs with a classic-mode vcpkg, configures with the Visual Studio generator,
  builds build/cpu, runs the tests, and deploys the Qt runtime.

  Prereqs: VS 2022 (MSVC), CMake >= 3.25, Python 3 (for aqtinstall), git.
  No CUDA needed (CPU build). For a GPU build see docs/STATUS.md.

.EXAMPLE
  pwsh scripts/build-cpu.ps1
#>
[CmdletBinding()]
param(
  [string]$QtVersion = "6.6.3",
  [string]$OpenCVVersion = "4.9.0",
  [string]$OrtVersion = "1.17.3",
  [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $repo
$deps = Join-Path $repo 'build\deps'
New-Item -ItemType Directory -Force $deps | Out-Null

function Step($m) { Write-Host "==> $m" -ForegroundColor Cyan }

# --- submodules (llama.cpp / whisper.cpp / hnswlib / miniaudio) --------------
Step "submodules"
git submodule update --init --recursive 2>$null
# Vendored sqlite amalgamation must be present (committed under third_party/sqlite3).
if (-not (Test-Path "$repo\third_party\sqlite3\sqlite3.c")) {
  throw "third_party/sqlite3/sqlite3.c missing (it is committed; check your checkout)."
}

# --- Qt (prebuilt, via aqtinstall) -------------------------------------------
$qtDir = "C:\Qt\$QtVersion\msvc2019_64"
if (-not (Test-Path "$qtDir\lib\cmake\Qt6\Qt6Config.cmake")) {
  Step "installing Qt $QtVersion via aqtinstall"
  python -m pip install -q aqtinstall
  python -m aqt install-qt windows desktop $QtVersion win64_msvc2019_64 -O C:\Qt -m qtmultimedia qtimageformats qtshadertools qthttpserver qtwebsockets
}
# QtHttpServer + QtWebSockets back the mobile gateway (src/gateway). They are
# add-on modules not in the base kit; install them if a prior run predates them.
if (-not (Test-Path "$qtDir\lib\cmake\Qt6HttpServer\Qt6HttpServerConfig.cmake")) {
  Step "installing Qt HttpServer + WebSockets modules (mobile gateway)"
  python -m pip install -q aqtinstall
  python -m aqt install-qt windows desktop $QtVersion win64_msvc2019_64 -O C:\Qt -m qthttpserver qtwebsockets
}

# --- OpenCV (prebuilt) -------------------------------------------------------
$cvCfg = "$deps\opencv\build\x64\vc16\lib"
if (-not (Test-Path "$cvCfg\OpenCVConfig.cmake")) {
  Step "downloading OpenCV $OpenCVVersion"
  $exe = "$deps\opencv.exe"
  Invoke-WebRequest "https://github.com/opencv/opencv/releases/download/$OpenCVVersion/opencv-$OpenCVVersion-windows.exe" -OutFile $exe -UseBasicParsing
  & $exe "-o$deps" -y | Out-Null
}

# --- ONNX Runtime (CPU) ------------------------------------------------------
$ortRoot = "$deps\onnxruntime-win-x64-$OrtVersion"
if (-not (Test-Path "$ortRoot\lib\onnxruntime.lib")) {
  Step "downloading ONNX Runtime $OrtVersion (CPU)"
  $zip = "$deps\ort.zip"
  Invoke-WebRequest "https://github.com/microsoft/onnxruntime/releases/download/v$OrtVersion/onnxruntime-win-x64-$OrtVersion.zip" -OutFile $zip -UseBasicParsing
  Expand-Archive -Force $zip $deps
}

# --- vcpkg (classic) for the small CONFIG libs -------------------------------
$vcpkg = "$repo\third_party\vcpkg"
if (-not (Test-Path "$vcpkg\vcpkg.exe")) {
  Step "bootstrapping vcpkg"
  if (-not (Test-Path "$vcpkg\.git")) { git clone --depth 1 https://github.com/microsoft/vcpkg $vcpkg }
  & "$vcpkg\bootstrap-vcpkg.bat" -disableMetrics | Out-Null
}
if (-not (Test-Path "$vcpkg\installed\x64-windows\share\spdlog\spdlogConfig.cmake")) {
  Step "vcpkg install small libs (classic mode)"
  Push-Location $env:TEMP   # run outside the repo so vcpkg.json doesn't force manifest mode
  & "$vcpkg\vcpkg.exe" install sqlite3 nlohmann-json fmt spdlog libsamplerate openssl --triplet x64-windows --vcpkg-root $vcpkg
  Pop-Location
}
# OpenSSL (libcrypto) is the crypto backend for the vendored SQLCipher
# amalgamation (third_party/sqlcipher) that enables real at-rest DB encryption.
if (-not (Test-Path "$vcpkg\installed\x64-windows\lib\libcrypto.lib")) {
  Step "vcpkg install openssl (SQLCipher crypto backend)"
  Push-Location $env:TEMP
  & "$vcpkg\vcpkg.exe" install openssl --triplet x64-windows --vcpkg-root $vcpkg
  Pop-Location
}

# --- configure + build -------------------------------------------------------
Step "cmake configure (Visual Studio 17 2022, CPU)"
cmake -S $repo -B "$repo\build\cpu" -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_BUILD_TYPE=Release `
  -DPOLYMATH_USE_CUDA=OFF -DGGML_CUDA=OFF -DPOLYMATH_BUILD_TESTS=ON `
  -DCMAKE_TOOLCHAIN_FILE="$vcpkg\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_MANIFEST_MODE=OFF -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DCMAKE_PREFIX_PATH="$qtDir" `
  -DOpenCV_DIR="$cvCfg" `
  -DPOLYMATH_ONNXRUNTIME_ROOT="$ortRoot"

Step "cmake build (this also builds llama.cpp + whisper.cpp from source)"
cmake --build "$repo\build\cpu" --config Release -j 6

if (-not $SkipTests) {
  Step "ctest"
  $bin = "$repo\build\cpu\bin\Release"
  $env:PATH = "$qtDir\bin;$ortRoot\lib;$deps\opencv\build\x64\vc16\bin;$bin;$env:PATH"
  ctest --test-dir "$repo\build\cpu" -C Release --output-on-failure
}

# --- deploy Qt runtime + extra DLLs next to the exe --------------------------
Step "windeployqt + runtime DLLs"
$bin = "$repo\build\cpu\bin\Release"
& "$qtDir\bin\windeployqt.exe" --qmldir "$repo\src\ui\qml" --no-translations "$bin\Polymath.exe" | Out-Null
Copy-Item "$qtDir\plugins\platforms\qoffscreen.dll" "$bin\platforms\" -Force -ErrorAction SilentlyContinue
Copy-Item "$deps\opencv\build\x64\vc16\bin\opencv_world*.dll" $bin -Force
Copy-Item "$deps\opencv\build\x64\vc16\bin\opencv_videoio_ffmpeg*.dll" $bin -Force -ErrorAction SilentlyContinue
# OpenSSL libcrypto DLL — the SQLCipher codec links it for at-rest encryption.
Copy-Item "$vcpkg\installed\x64-windows\bin\libcrypto-3-x64.dll" $bin -Force -ErrorAction SilentlyContinue

Write-Host "`nDone -> $bin\Polymath.exe" -ForegroundColor Green
Write-Host "Next: pwsh scripts/fetch-models.ps1  (download local models), then run Polymath.exe" -ForegroundColor Green
