# Overhaul 2 — Resume handoff

## Standing agent rule (survives compact)
Do not stop until DAG is complete. Parallel subagents on disjoint files;
orchestrator builds/tests/commits. Build via `C:\pm`. Always `ctest -j1`.

## Status: **SHIPPED**
All waves A–F complete. Tag: `v0.3.0-overhaul2`.

### F2 live proof (2026-07-11)
- `youtube_search` live: castles / trains / cooking → 6 results each
- `watch_video` / `slop_mode` skill chains with `{{result:…}}` refs
- TTS Kokoro: `af_heart` + `af_sky` synthesize non-empty PCM
- Suite: `test_f2_youtube_tts` (ctest name `f2_youtube_tts`)

### Binaries
- CPU: `C:\pm\build\cpu\bin\Release\Polymath.exe`
- GPU: `C:\pm\build\cuda\bin\Polymath.exe`

### Installer (if ISCC present)
```
ISCC /DAppVersion=0.3.0 /DFlavor=cuda scripts\installer\polymath.iss
```
