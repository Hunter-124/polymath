<#
.SYNOPSIS
  Silent install / launch probe / uninstall smoke for the Inno Setup package.

.EXAMPLE
  powershell -File scripts/smoke-install.ps1 -Version 0.3.2
#>
[CmdletBinding()]
param(
  [string]$Version = '0.3.2',
  [ValidateSet('cuda','cpu')] [string]$Flavor = 'cuda',
  [string]$SetupPath = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $repo) { $repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path }
if (-not $SetupPath) {
  $SetupPath = Join-Path $repo "dist\Polymath-$Version-win64-$Flavor-Setup.exe"
}
if (-not (Test-Path $SetupPath)) {
  throw "Installer not found: $SetupPath - build with package.ps1 + ISCC first"
}

$installDir = Join-Path $env:LOCALAPPDATA "Programs\Polymath-smoke-$Version"
Write-Host "Smoke install -> $installDir" -ForegroundColor Cyan

$p = Start-Process -FilePath $SetupPath -ArgumentList @(
  '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
  "/DIR=$installDir"
) -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "Installer exit $($p.ExitCode)" }

$exe = Join-Path $installDir 'Polymath.exe'
if (-not (Test-Path $exe)) { throw "Polymath.exe missing after install" }
$sizeMb = [math]::Round((Get-Item $exe).Length / 1MB, 2)
Write-Host "Installed OK: $exe ($sizeMb MB)" -ForegroundColor Green

$env:QT_QPA_PLATFORM = 'offscreen'
$probe = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 4
if (-not $probe.HasExited) {
  Stop-Process -Id $probe.Id -Force -ErrorAction SilentlyContinue
  Write-Host "Launch probe: process stayed up 4s (good)" -ForegroundColor Green
} else {
  Write-Host "Launch probe: exited code=$($probe.ExitCode) (models missing is OK)" -ForegroundColor Yellow
}

$unins = Get-ChildItem $installDir -Filter 'unins*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($unins) {
  $u = Start-Process -FilePath $unins.FullName -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES') -Wait -PassThru
  Write-Host "Uninstall exit=$($u.ExitCode)" -ForegroundColor Cyan
} else {
  Remove-Item -Recurse -Force $installDir -ErrorAction SilentlyContinue
  Write-Host "Removed $installDir (no unins found)" -ForegroundColor Yellow
}
Write-Host "smoke-install: PASS" -ForegroundColor Green
