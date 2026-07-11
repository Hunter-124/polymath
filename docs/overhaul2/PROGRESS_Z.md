# Wave Z — progress ledger (EVERYTHING)

> Target tags: `v0.3.1-wavez` (first residual ship), `v0.3.2-complete` (no leftovers).

Legend: [ ] pending · [~] in progress · [x] done

## Z0 — Hygiene
- [x] LICENSE (MIT) + THIRD_PARTY_NOTICES
- [x] models.id UNIQUE quiet
- [x] GGUF native picker
- [x] docs

## Z1 — Safety
- [x] fs_write undo + fs_undo
- [x] browser allowlist + block_file + session wipe

## Z2 — YouTube + Memory
- [x] SponsorBlock
- [x] Settings toggles
- [x] MemoryView

## Z3 — Identity + advisor
- [x] active_user_id + AgentLoop
- [x] memory namespace filter
- [x] CamerasView create/enroll users
- [x] calendar_read (.ics)
- [x] inbox_notes + **email_fetch (IMAP SSL)**

## Z4 — Capture / voice
- [x] Window capture (existing) + adaptive AEC-lite barge-in
- [x] VRAM honesty (sequential goal-tree on 8 GB)

## Z5 — Package & channel
- [x] CUDA ORT download (`scripts/fetch-ort-cuda.ps1`) + package DLL bundling
- [x] Auto-update client (`checkForUpdates`, Settings)
- [x] sign-release.ps1 + **self-signed local cert + signed installer**
- [x] smoke-install.ps1
- [x] Installer 0.3.2
- [x] tag `v0.3.2-complete`

## Tools
**46** builtins (incl. fs_undo, calendar_read, inbox_notes, email_fetch).

## Only remaining external (not code)
- Public SmartScreen reputation still needs a **CA-issued** OV/EV cert (self-signed is signed & tamper-evident but not trusted by SmartScreen).
- IMAP needs **your** host + app password in Settings keys.
- Update feed needs a hosted `latest.json` URL you control.
