# Build status

**Overhaul 2 → v0.3.0** + **Wave Z → v0.3.1**. Master plan: [`docs/overhaul2/00_MASTER_PLAN.md`](overhaul2/00_MASTER_PLAN.md).
Wave Z ledger: [`docs/overhaul2/PROGRESS_Z.md`](overhaul2/PROGRESS_Z.md). Handoff: [`docs/overhaul2/HANDOFF.md`](overhaul2/HANDOFF.md).

Hardware truth and resource budget: [`docs/overhaul/04_VOICE_RESOURCES.md`](overhaul/04_VOICE_RESOURCES.md) §1.

## Hardware (this machine)

| Resource | Value |
|---|---|
| CPU | Intel i7-9750H, 6C/12T |
| RAM | 32 GB |
| GPU | **RTX 2070 Max-Q, 8 GB VRAM (sm_75)** |
| Use profile | Dedicated to Polymath + browser/video research |

Steady-state VRAM budget (Fast @ 4096 ctx + q8 KV + Embedding + OS) ≈ **5.7–6.2 GB**.

## Overhaul 2 gates

| Gate | Status | Notes |
|------|--------|--------|
| Waves A–E (all nodes) | ✅ green | A1–A4, B1–B4, C1–C3, D1–D4, E1–E5, EV |
| CPU Release + serial ctest | ✅ green | `build/cpu`, **22/22** suites (`ctest -j1`, incl. `f2_youtube_tts`) |
| `capture_views` | ✅ green | **19/19** PNGs (`captures_overhaul2/`) |
| GPU tree (CUDA arch 75) | ✅ green | `scripts/build-gpu.ps1` → `build/cuda`; sm_75, `ggml-cuda` |
| F2 live e2e | ✅ green | youtube_search ×3 + skill chains + Kokoro af_heart/af_sky (`f2_youtube_tts`) |
| F3 tag + installer | ✅ green | tag `v0.3.0-overhaul2`; `Polymath-0.3.0-win64-cuda-Setup.exe` via ISCC |
| Wave Z (residual + backlog) | ✅ green | LICENSE, undo journal, browser allowlist, SponsorBlock, Memory UI, calendar/inbox tools, sign/smoke scripts; **45** tools; tag `v0.3.1-wavez` |
| Wave Z installer | ✅ green | `Polymath-0.3.1-win64-cuda-Setup.exe` (unsigned; `scripts/sign-release.ps1` ready) |

## Build recipes

```powershell
# CPU (VS 18 2026 + C:\pm junction)
cmake --build C:/pm/build/cpu --config Release -j 6
ctest -C Release -j1 --output-on-failure   # always serial for heavy suites

# UI-only captures
$env:QT_QPA_PLATFORM='offscreen'
C:/pm/build/cpu/bin/Release/capture_views.exe <outdir>

# GPU
pwsh scripts/build-gpu.ps1 -Arch 75   # → build/cuda/bin/Polymath.exe
```

## What Overhaul 2 landed

| Area | Landed |
|------|--------|
| **Harness** | Router v2 + final-answer hygiene; goal resume/rejoin; SafetyPolicy risk-gate + waiting_user; goal-tree spawn_subtask/join policies |
| **YouTube** | `youtube_search` (Innertube); VideoPickerSurface; adblock/YtClean; `watch_video` skill with `{{result:…}}` step chaining; slop_mode top-hit autoplay |
| **Computer use** | Confirm dialog + Settings ▸ Safety; fs_*/run_command/app_launch/clipboard; screen_capture/describe (privacy.screen_capture) |
| **Scheduler v2** | timed/recurring agent goals; schedule_task tools; Tasks Scheduled UI |
| **Advisor** | Advisor persona + daily_briefing / standup / project_review / session_digest skills |
| **GUI** | Chat select+scroll; personalities editor; NoteSurface + research boards; window takeover (present/Esc/pill); copy polish |
| **TTS v2** | engine/voice/speed config + Settings Voice; default `af_heart` |
| **Tools** | **45** registered tools (+ `fs_undo`, `calendar_read`, `inbox_notes`) |
| **Wave Z** | MIT LICENSE; GGUF picker; fs undo; browser allowlist; SponsorBlock; MemoryView; identity.active_user_id; sign-release + smoke-install scripts |

## Tests (CPU Release, serial)

| Suite | Notes |
|-------|--------|
| core, tools, audio, agent, vision, inference, memory, privacy, integration, ui | legacy e2e |
| j_phase2, harness, skills, youtube_search, sessions, adblock | overhaul 1–2 |
| goals, router, scheduler_v2, safety_policy, system_tools | overhaul 2 |

Always run **`ctest -j1`** — audio/inference/memory contend under `-j4`.
