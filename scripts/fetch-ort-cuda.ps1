<#
.SYNOPSIS
  Download ONNX Runtime GPU (CUDA) package next to the CPU ORT tree for vision EP.

.DESCRIPTION
  Fetches microsoft/onnxruntime win-x64 GPU build into build/deps/onnxruntime-win-x64-gpu-*
  and writes a marker so package.ps1 / build-gpu can point POLYMATH_ONNXRUNTIME_ROOT at it.

.EXAMPLE
  powershell -File scripts/fetch-ort-cuda.ps1
#>
[CmdletBinding()]
param(
  [string]$Version = '1.17.3',
  [string]$OutRoot = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $repo) { $repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path }
if (-not $OutRoot) { $OutRoot = Join-Path $repo 'build\deps' }

# Prefer CUDA 12 package (matches modern system CUDA 12/13 toolkits for EP).
$names = @(
  "onnxruntime-win-x64-gpu-cuda12-$Version",
  "onnxruntime-win-x64-gpu-$Version"
)
$base = "https://github.com/microsoft/onnxruntime/releases/download/v$Version"

New-Item -ItemType Directory -Force -Path $OutRoot | Out-Null
$destName = $null
$zipPath = $null
foreach ($n in $names) {
  $url = "$base/$n.zip"
  $zip = Join-Path $OutRoot "$n.zip"
  Write-Host "Trying $url ..." -ForegroundColor Cyan
  try {
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    if ((Get-Item $zip).Length -gt 1MB) {
      $destName = $n
      $zipPath = $zip
      break
    }
  } catch {
    Write-Host "  miss: $($_.Exception.Message)" -ForegroundColor Yellow
  }
}
if (-not $zipPath) { throw "Could not download ORT GPU $Version from GitHub releases" }

$dest = Join-Path $OutRoot $destName
if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
Expand-Archive -Path $zipPath -DestinationPath $OutRoot -Force
# Some zips nest one folder; normalize.
if (-not (Test-Path (Join-Path $dest 'lib'))) {
  $inner = Get-ChildItem $OutRoot -Directory | Where-Object { $_.Name -like 'onnxruntime*' } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if ($inner -and $inner.FullName -ne $dest) {
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    Rename-Item $inner.FullName $destName
  }
}
# Marker for build scripts
Set-Content (Join-Path $OutRoot 'ort-cuda-root.txt') $dest
Write-Host "ORT CUDA ready: $dest" -ForegroundColor Green
Write-Host "Set POLYMATH_ONNXRUNTIME_ROOT=$dest before cmake configure (or re-run build-gpu.ps1)." -ForegroundColor Cyan
Get-ChildItem $dest -Recurse -Filter '*.dll' | Select-Object -First 15 FullName
