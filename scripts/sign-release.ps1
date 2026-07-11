<#
.SYNOPSIS
  Sign Polymath.exe and/or the Inno installer with Authenticode (Wave Z).

.DESCRIPTION
  Requires signtool (Windows SDK) and a code-signing cert (.pfx or cert store).
  Without a cert, use -DryRun to print the exact commands that would run.

.EXAMPLE
  powershell -File scripts/sign-release.ps1 -Pfx C:\certs\polymath.pfx -Password env:PM_SIGN_PW
  powershell -File scripts/sign-release.ps1 -DryRun
#>
[CmdletBinding()]
param(
  [string]$Pfx = '',
  [string]$Password = '',
  [string]$Thumbprint = '',
  [string]$Version = '0.3.1',
  [ValidateSet('cuda','cpu')] [string]$Flavor = 'cuda',
  [string]$TimestampUrl = 'http://timestamp.digicert.com',
  [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $repo) { $repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path }

$signtool = @(
  "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe",
  "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe",
  "${env:ProgramFiles(x86)}\Windows Kits\10\App Certification Kit\signtool.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $signtool) {
  $found = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
  if ($found) { $signtool = $found }
}

$bundle = Join-Path $repo "dist\Polymath-$Version-win64-$Flavor"
$exe = Join-Path $bundle 'Polymath.exe'
$setup = Join-Path $repo "dist\Polymath-$Version-win64-$Flavor-Setup.exe"

function Invoke-Sign([string]$Target) {
  if (-not (Test-Path $Target)) { Write-Warning "Missing $Target"; return }
  $args = @('sign', '/fd', 'SHA256', '/tr', $TimestampUrl, '/td', 'SHA256')
  if ($Thumbprint) {
    $args += @('/sha1', $Thumbprint)
  } elseif ($Pfx) {
    $args += @('/f', $Pfx)
    if ($Password) { $args += @('/p', $Password) }
  } else {
    throw "Provide -Pfx or -Thumbprint (or use -DryRun)"
  }
  $args += $Target
  if ($DryRun) {
    Write-Host "DRY-RUN: $signtool $($args -join ' ')" -ForegroundColor Yellow
    return
  }
  if (-not $signtool) { throw "signtool.exe not found (install Windows SDK)" }
  & $signtool @args
  if ($LASTEXITCODE -ne 0) { throw "signtool failed ($LASTEXITCODE) for $Target" }
  Write-Host "Signed $Target" -ForegroundColor Green
}

Write-Host "sign-release: version=$Version flavor=$Flavor dryRun=$DryRun" -ForegroundColor Cyan
if ($DryRun -and -not $Pfx -and -not $Thumbprint) {
  Write-Host "DRY-RUN (no cert): would sign:" -ForegroundColor Yellow
  Write-Host "  $exe"
  Write-Host "  $setup"
  Write-Host "Obtain an OV/EV Authenticode cert, then re-run with -Pfx or -Thumbprint."
  exit 0
}
Invoke-Sign $exe
Invoke-Sign $setup
Write-Host "Done." -ForegroundColor Green
