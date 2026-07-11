# Overhaul 2 — progress ledger

> **RESUME POINTER:** Waves A–E + F1 done. **F2 needs owner sign-off** (YouTube demo + TTS).
> Then F3 tag is ready (`v0.3.0-overhaul2`). Checklist: `results/F2_e2e.md`.

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
- [x] F1 full builds (CPU + CUDA) + ctest 21/21 + captures 19/19
- [~] F2 live e2e — automated pre-checks green; **owner: YouTube + TTS**
- [~] F3 docs + graphify done; tag + installer after F2 sign-off

## Notes
- Serial ctest always (`-j1`). Tool count = 42.
- GPU: `build/cuda` Ninja/CUDA 13.3 arch 75, `ggml-cuda.dll` present.
- graphify-out rebuilt (13515 nodes). ISCC not installed on this machine.
