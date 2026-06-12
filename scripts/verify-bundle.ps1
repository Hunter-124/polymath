<#
.SYNOPSIS
  Verify a staged Hearth bundle is truly self-contained.

.DESCRIPTION
  Walks the PE import table of every .exe/.dll in the bundle (dumpbin
  /dependents) and fails if anything imports a DLL that is neither present in
  the bundle nor a core Windows component.

  WHY THIS EXISTS: the 0.1.0 installers shipped without msvcp140_2.dll (needed
  by Qt6Gui/Qt6Quick) and vcomp140.dll (OpenMP, needed by ggml). On the build
  machine the system-wide VC++ redistributable silently satisfied those imports,
  so the bundle "worked here" — and did nothing at all when launched on a clean
  machine. This check makes that failure loud at packaging time instead.

  Debug-CRT imports (ucrtbased.dll, msvcp140d.dll, ...) are deliberately NOT in
  the allow-list: if they show up, a debug-built DLL leaked into the bundle.

.PARAMETER BundleDir  The staged bundle folder (dist\Hearth-<ver>-win64-<flavor>).
.EXAMPLE  pwsh scripts/verify-bundle.ps1 -BundleDir dist\Hearth-0.1.0-win64-cpu
#>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$BundleDir)
$ErrorActionPreference = 'Stop'

if (-not (Test-Path $BundleDir)) { throw "Bundle dir not found: $BundleDir" }

# dumpbin ships with VS; pick an x64-hosted one. Without VS, skip (warn) — the
# packaging box always has VS, so in practice this never silently skips there.
$dumpbin = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\2022" -Recurse `
             -Filter dumpbin.exe -ErrorAction SilentlyContinue |
           Where-Object FullName -match 'HostX64\\x64' |
           Select-Object -First 1 -ExpandProperty FullName
if (-not $dumpbin) {
  Write-Warning "dumpbin.exe (VS 2022) not found - skipping bundle import verification."
  exit 0
}

$files = Get-ChildItem $BundleDir -Recurse -File | Where-Object { $_.Extension -in '.dll', '.exe' }
$present = @{}
foreach ($f in $files) { $present[$f.Name.ToLower()] = $true }

# DLLs every supported Windows (10+) ships in System32. UCRT forwarders
# (api-ms-win-*/ext-ms-*) and ucrtbase are OS components on Win10+.
$osKnown = '^(ntdll|kernel32|kernelbase|user32|gdi32|gdiplus|shell32|shcore|shlwapi|ole32|oleaut32|oleacc|advapi32|ws2_32|crypt32|bcrypt|ncrypt|secur32|comdlg32|comctl32|winmm|version|setupapi|iphlpapi|dwmapi|uxtheme|dxgi|dwrite|d2d1|d3d9|d3d11|d3d12|d3dcompiler_47|wtsapi32|userenv|netapi32|authz|dnsapi|winhttp|wininet|urlmon|propsys|imm32|mpr|wldap32|psapi|powrprof|pdh|cfgmgr32|rpcrt4|msvcrt|normaliz|dbghelp|imagehlp|msimg32|opengl32|glu32|wbemuuid|wevtapi|hid|winusb|usp10|windowscodecs|mf|mfplat|mfreadwrite|mfcore|avicap32|msacm32|vfw32|winspool|ucrtbase|api-ms-win-.*|ext-ms-.*)\.dll$'

$missing = @{}
foreach ($f in $files) {
  & $dumpbin /dependents $f.FullName 2>$null |
    Where-Object { $_ -match '^\s+\S+\.dll\s*$' } |
    ForEach-Object {
      $dep = $_.Trim().ToLower()
      if (-not $present.ContainsKey($dep) -and $dep -notmatch $osKnown) {
        if (-not $missing.ContainsKey($dep)) { $missing[$dep] = [System.Collections.Generic.List[string]]::new() }
        if ($missing[$dep] -notcontains $f.Name) { $missing[$dep].Add($f.Name) }
      }
    }
}

if ($missing.Count -eq 0) {
  Write-Host "verify-bundle: OK - all $($files.Count) binaries resolve inside the bundle (or core Windows)." -ForegroundColor Green
  exit 0
}

Write-Host "verify-bundle: FAIL - the bundle is NOT self-contained:" -ForegroundColor Red
foreach ($dep in ($missing.Keys | Sort-Object)) {
  Write-Host ("  {0}  <- imported by: {1}" -f $dep, (($missing[$dep] | Select-Object -First 5) -join ', ')) -ForegroundColor Red
}
Write-Host "On a machine without the VC++ redist / dev tools these imports fail and the app will not launch." -ForegroundColor Red
exit 1
