# Overhaul 2 — Resume handoff

## Standing agent rule (survives compact)
Do not stop until DAG is complete. Parallel subagents on disjoint files;
orchestrator builds/tests/commits. Build via `C:\pm`. Always `ctest -j1`.

## Status: **SHIPPED** (Overhaul 2 + Wave Z)
All waves A–F complete. Tag: `v0.3.0-overhaul2`.
Wave Z residual/backlog closed. Tag: `v0.3.1-wavez`. See `PROGRESS_Z.md`.

### F2 live proof (2026-07-11)
- `youtube_search` live: castles / trains / cooking → 6 results each
- `watch_video` / `slop_mode` skill chains with `{{result:…}}` refs
- TTS Kokoro: `af_heart` + `af_sky` synthesize non-empty PCM
- Suite: `test_f2_youtube_tts` (ctest name `f2_youtube_tts`)

### Binaries
- CPU: `C:\pm\build\cpu\bin\Release\Polymath.exe`
- GPU: `C:\pm\build\cuda\bin\Polymath.exe`

### Installer (built 2026-07-11)
```
powershell -File scripts\package.ps1 -Flavor cuda -NoZip
& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" /DAppVersion=0.3.1 /DFlavor=cuda scripts\installer\polymath.iss
# optional signing when you have a cert:
powershell -File scripts\sign-release.ps1 -Version 0.3.1 -Pfx path\to.pfx
# silent install smoke:
powershell -File scripts\smoke-install.ps1 -Version 0.3.1
```
Artifacts (not in git):
- `dist\Polymath-0.3.1-win64-cuda\` (portable stage, ~338 MB)
- `dist\Polymath-0.3.1-win64-cuda-Setup.exe` (~127 MB)

### Wave Z highlights
- 45 tools; Memory UI; SponsorBlock; fs_undo; browser allowlist; calendar_read / inbox_notes
- MIT LICENSE; GGUF native picker; identity.active_user_id
