# Overhaul 2 — progress ledger

> **COMPLETE:** Waves A–F done. Tag `v0.3.0-overhaul2`. F2 YouTube + TTS live-verified.

## Wave A — harness correctness
- [x] A1 A2 A3 A4

## Wave B — YouTube pipeline
- [x] B1 B2 B3 B4

## Wave C — computer use + safeguards
- [x] C1 C2 C3

## Wave D — harness expansion
- [x] D1 D2 D3 D4

## Wave E — GUI features
- [x] E1 E2 E3 E4 E5 EV

## Wave F — verify & ship
- [x] F1 full builds (CPU + CUDA) + ctest + captures
- [x] F2 live e2e — youtube_search ×3 + skill chains + TTS af_heart/af_sky
- [x] F3 docs + graphify + tag v0.3.0-overhaul2

## Notes
- Serial ctest always (`-j1`). Tool count = 42. +`f2_youtube_tts` suite.
- Skills seed now merges missing starters (watch_video on upgrades).
- Default TTS voice: `af_heart` (Kokoro).
