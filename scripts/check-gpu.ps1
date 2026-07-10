<#
.SYNOPSIS
  Detect the GPU/driver situation and report whether Polymath should run the
  CUDA (GPU) build or fall back to CPU. Used by the first-run wizard and runnable
  standalone for a quick "will the GPU build work here?" answer.

.DESCRIPTION
  Polymath ships in two flavours: a CUDA build (llama/whisper offload to an NVIDIA
  GPU) and a CPU build (no GPU required, slower). This script:
    - finds nvidia-smi and reads the GPU name, driver version, total/free VRAM,
    - flags the ~8 GB VRAM budget the InferenceManager targets (Fast model
      resident on-GPU; Heavy/Vision spill to CPU below that),
    - returns a verdict object so the wizard can pick a flavour and warn honestly.

  No GPU / no driver is NOT an error — the CPU build runs everywhere. This only
  tells the user what to expect.

.PARAMETER Quiet   Suppress the human-readable report; just return the object.

.OUTPUTS
  A PSCustomObject: HasNvidiaGpu, DriverVersion, GpuName, VramTotalMB, VramFreeMB,
  Recommend ('cuda'|'cpu'), Notes (string[]).

.EXAMPLE  pwsh scripts/check-gpu.ps1
.EXAMPLE  $gpu = pwsh scripts/check-gpu.ps1 -Quiet
#>
[CmdletBinding()]
param([switch]$Quiet)

# The InferenceManager keeps the Fast model (~5 GB resident for Gemma 3n E4B Q4)
# on-GPU and trims n_gpu_layers for Heavy/Vision to fit. Below this much free
# VRAM the CUDA build still runs but spills more to CPU.
$VramBudgetMB = 8192

$result = [PSCustomObject]@{
  HasNvidiaGpu  = $false
  DriverVersion = $null
  GpuName       = $null
  VramTotalMB   = 0
  VramFreeMB    = 0
  Recommend     = 'cpu'
  Notes         = @()
}

# nvidia-smi is installed alongside the NVIDIA driver (in System32 even without
# the CUDA toolkit), so its presence is a reliable "an NVIDIA driver is loaded".
$smi = (Get-Command nvidia-smi -ErrorAction SilentlyContinue).Source
if (-not $smi) {
  $sys = Join-Path $env:WINDIR 'System32\nvidia-smi.exe'
  if (Test-Path $sys) { $smi = $sys }
}

if ($smi) {
  try {
    $q = & $smi --query-gpu=name,driver_version,memory.total,memory.free `
                --format=csv,noheader,nounits 2>$null
    if ($LASTEXITCODE -eq 0 -and $q) {
      $row = ($q | Select-Object -First 1) -split '\s*,\s*'
      $result.HasNvidiaGpu  = $true
      $result.GpuName       = $row[0]
      $result.DriverVersion = $row[1]
      $result.VramTotalMB   = [int]$row[2]
      $result.VramFreeMB    = [int]$row[3]
      $result.Recommend     = 'cuda'
      if ($result.VramTotalMB -lt $VramBudgetMB) {
        $result.Notes += "GPU has $($result.VramTotalMB) MB VRAM (< ${VramBudgetMB} MB budget): the Fast model fits but Heavy/Vision will offload partly to CPU."
      } else {
        $result.Notes += "GPU VRAM ($($result.VramTotalMB) MB) covers the ~${VramBudgetMB} MB budget — Fast model fully on-GPU."
      }
    } else {
      $result.Notes += "nvidia-smi found but returned no GPU (driver issue?). Falling back to the CPU build."
    }
  } catch {
    $result.Notes += "nvidia-smi failed to run: $($_.Exception.Message). Falling back to the CPU build."
  }
} else {
  $result.Notes += "No NVIDIA driver detected (nvidia-smi not found). Use the CPU build — it runs without a GPU (LLM inference is slower)."
}

if (-not $Quiet) {
  Write-Host "== GPU / driver check ==" -ForegroundColor Cyan
  if ($result.HasNvidiaGpu) {
    Write-Host ("  GPU:    {0}" -f $result.GpuName) -ForegroundColor Green
    Write-Host ("  Driver: {0}" -f $result.DriverVersion)
    Write-Host ("  VRAM:   {0} MB total, {1} MB free" -f $result.VramTotalMB, $result.VramFreeMB)
  } else {
    Write-Host "  No NVIDIA GPU usable here." -ForegroundColor Yellow
  }
  foreach ($n in $result.Notes) { Write-Host "  - $n" -ForegroundColor DarkGray }
  Write-Host ("  Recommended build: {0}" -f $result.Recommend.ToUpper()) -ForegroundColor Cyan
}

return $result
