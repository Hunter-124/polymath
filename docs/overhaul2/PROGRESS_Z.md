# Wave Z — progress ledger (ship everything residual)

> Follow-on to Overhaul 2 (`v0.3.0-overhaul2`). Target tag: `v0.3.1-wavez`.

Legend: [ ] pending · [~] in progress · [x] done · [!] blocked

## Z0 — Hygiene
- [x] Z0a LICENSE + THIRD_PARTY_NOTICES
- [x] Z0b models.id UNIQUE spam (`INSERT OR IGNORE` + path normalize)
- [x] Z0c GGUF native Win32 picker (`pickAndAddModel`)
- [x] Z0d docs STATUS/SHIP (this ledger)

## Z1 — Safety
- [x] Z1a fs_write undo journal + `fs_undo` tool
- [x] Z1b browser_drive allowlist + block_file
- [x] Z1c browser session wipe on close

## Z2 — YouTube + Memory UI
- [x] Z2a SponsorBlock in YtClean
- [x] Z2b video.sponsorblock setting (Settings ▸ Safety)
- [x] Z2c MemoryView dashboard + nav rail

## Z3 — Identity + advisor inputs
- [x] Z3a Face → active_user_id (settings `identity.active_user_id` + AgentLoop)
- [x] Z3b Memory user namespace filter in keyword recall
- [~] Z3c Enroll UX polish (API exists; full camera GUI polish deferred)
- [x] Z3d calendar_read (.ics)
- [x] Z3e inbox_notes (local drop folder)

## Z4 — Capture / voice / inference
- [x] Z4a Window capture already present (title match) — kept
- [x] Z4b DXGI optional — documented as Qt path sufficient for agent stills
- [x] Z4c Barge-in v1 already shipped; half-duplex documented
- [x] Z4d VRAM honesty: no dual-GPU Fast on 8 GB (goal-tree sequential)
- [x] Z4e Heavy swap path already in InferenceManager

## Z5 — Package & channel
- [x] Z5a Mobile gateway already wired (LAN)
- [~] Z5b CUDA ORT optional — code EP ready; package flag deferred
- [~] Z5c Auto-update client — config keys seeded; full downloader deferred
- [x] Z5d sign-release.ps1 (+ DryRun without cert)
- [x] Z5e smoke-install.ps1
- [x] Z5f 0.3.1 installer (`dist\Polymath-0.3.1-win64-cuda-Setup.exe` ~127 MB)
- [x] Z5g tag v0.3.1-wavez

## Blockers (honest)
- Code signing **execution** needs owner Authenticode cert (`scripts/sign-release.ps1` ready).
- Tools count = **45**.
