<#
.SYNOPSIS
  One-shot developer bootstrap for Polymath on Windows.

.DESCRIPTION
  Checks prerequisites, initializes git submodules, sets up vcpkg, helps locate
  ONNX Runtime (GPU), and runs the first CMake configure. Idempotent.

.EXAMPLE
  pwsh scripts/setup-dev.ps1 -VcpkgRoot C:\dev\vcpkg -OnnxRoot C:\dev\onnxruntime-gpu
#>
[CmdletBinding()]
param(
  [string]$VcpkgRoot = $env:VCPKG_ROOT,
  [string]$OnnxRoot  = $env:ONNXRUNTIME_ROOT,
  [string]$Preset    = 'cuda-release'
)
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..')
Set-Location $repo

function Need($name, $hint) {
  if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
    throw "Missing '$name'. $hint"
  }
  Write-Host "  [ok] $name" -ForegroundColor Green
}

Write-Host "== Prerequisites ==" -ForegroundColor Cyan
Need 'git'   'Install Git for Windows.'
Need 'cmake' 'Install CMake >= 3.25 and add to PATH.'
Need 'ninja' 'Install Ninja (comes with VS 2022 / or scoop install ninja).'
if (-not (Get-Command 'nvcc' -ErrorAction SilentlyContinue)) {
  Write-Warning "  nvcc (CUDA Toolkit) not found — CUDA presets will fail. Use -Preset cpu-release for a GPU-less build."
} else { Write-Host "  [ok] nvcc (CUDA)" -ForegroundColor Green }

Write-Host "`n== Submodules ==" -ForegroundColor Cyan
git submodule update --init --recursive

Write-Host "`n== vcpkg ==" -ForegroundColor Cyan
if (-not $VcpkgRoot) {
  $VcpkgRoot = Join-Path $repo 'third_party\vcpkg'
  if (-not (Test-Path (Join-Path $VcpkgRoot '.git'))) {
    Write-Host "  cloning vcpkg into $VcpkgRoot"
    git clone https://github.com/microsoft/vcpkg $VcpkgRoot
    & (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
  }
}
$env:VCPKG_ROOT = $VcpkgRoot
Write-Host "  VCPKG_ROOT = $VcpkgRoot" -ForegroundColor Green

Write-Host "`n== ONNX Runtime ==" -ForegroundColor Cyan
if (-not $OnnxRoot -or -not (Test-Path $OnnxRoot)) {
  Write-Warning "  ONNXRUNTIME_ROOT not set / not found."
  Write-Warning "  Download onnxruntime-win-x64-gpu-*.zip from the ONNX Runtime releases,"
  Write-Warning "  extract it, and pass -OnnxRoot <folder> (the one with include\ and lib\)."
} else {
  $env:ONNXRUNTIME_ROOT = $OnnxRoot
  Write-Host "  ONNXRUNTIME_ROOT = $OnnxRoot" -ForegroundColor Green
}

Write-Host "`n== Configure ($Preset) ==" -ForegroundColor Cyan
cmake --preset $Preset
Write-Host "`nDone. Build with:  cmake --build --preset $Preset" -ForegroundColor Green
Write-Host "Then fetch models: pwsh scripts/fetch-models.ps1" -ForegroundColor Green
