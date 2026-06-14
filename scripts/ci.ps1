<#
.SYNOPSIS
  Hearth CI entry point — clean single-binary CPU build + the full ctest suite.

.DESCRIPTION
  This is what CI (and you, locally) run to prove the tree is green. It delegates
  to the one build script:

      scripts/build.ps1 -Flavor cpu -Tests

  which configures the single auto-detecting binary CPU-only (POLYMATH_BACKEND_DL,
  no CUDA), builds it + the tests, and runs the WHOLE ctest suite, exiting non-zero
  if anything is red. The one model-hard test (memory) is auto-excluded by build.ps1
  when the EmbeddingGemma model tree is absent (CI has no models).

  Exit code is the gate: 0 = all green, non-zero = build or a test failed.

  CI constraints (unchanged): CPU-only (no CUDA runner), models absent (the heavy
  model/GPU test halves are opt-in behind POLYMATH_E2E_FULL / POLYMATH_E2E_LLM and
  skip cleanly when unset), and headless (QT_QPA_PLATFORM=offscreen).

.PARAMETER Clean  Remove build/dist before building (true from-scratch). CI passes this.
.PARAMETER Full   Also run the opt-in model/GPU test halves (POLYMATH_E2E_FULL=1).

.EXAMPLE  pwsh scripts/ci.ps1 -Clean
.EXAMPLE  pwsh scripts/ci.ps1 -Full
#>
[CmdletBinding()]
param(
  [switch]$Clean,
  [switch]$Full
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $repo
function Step($m) { Write-Host "==> $m" -ForegroundColor Cyan }
function Fail($m) { Write-Host "XX  $m" -ForegroundColor Red }

$buildDir = Join-Path $repo 'build\dist'
$env:QT_QPA_PLATFORM = 'offscreen'
if ($Full) { $env:POLYMATH_E2E_FULL = '1' } else { $env:POLYMATH_E2E_FULL = '0' }

if ($Clean -and (Test-Path $buildDir)) {
  Step "clean: removing $buildDir"
  # Unlink any models junction first so the recursive delete never follows it.
  Get-ChildItem $buildDir -Recurse -Force -ErrorAction SilentlyContinue |
    Where-Object { $_.LinkType } |
    ForEach-Object { [System.IO.Directory]::Delete($_.FullName, $false) }
  [System.IO.Directory]::Delete($buildDir, $true)
}

# build.ps1 -Tests configures (tests ON) + builds + runs the full ctest suite and
# throws on any failure; with ErrorActionPreference=Stop that propagates here.
Step "build + test (scripts/build.ps1 -Flavor cpu -Tests)"
try {
  & (Join-Path $PSScriptRoot 'build.ps1') -Flavor cpu -Tests
} catch {
  Fail "CI failed: $($_.Exception.Message)"
  exit 1
}

Write-Host "`nCI GREEN — single-binary build + full ctest passed." -ForegroundColor Green
exit 0
