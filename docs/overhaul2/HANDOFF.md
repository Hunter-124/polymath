# Overhaul 2 — Resume handoff

## Standing agent rule (survives compact / auto-compact)
**Do not stop.** Keep executing DAG nodes via parallel subagents (edit-only,
disjoint files), integrate centrally, build through `C:\pm`, fix every failure,
`ctest -j1`, commit/push, and continue until **F3 is fully done** (tag
`v0.3.0-overhaul2` + installer if ISCC available). Owner-only gates (live
YouTube/TTS taste) block only those checkboxes — everything else must keep
moving. Re-read this file after any compaction.

## Done
All DAG nodes A1–A4, B1–B4, C1–C3, D1–D4, E1–E5, EV, F1 are landed on `master`.
- CPU: serial ctest **21/21**, capture_views **19/19**
- CUDA: `build/cuda/bin/Polymath.exe` + `ggml-cuda.dll` (arch 75)
- graphify updated under `graphify-out/`

## YOU ARE HERE — F2 → F3
1. Drive as much of `docs/overhaul2/results/F2_e2e.md` as possible without a
   human (unit/integration, skill expand, youtube_search fixture, captures).
2. **Owner taste gates** (cannot automate): YouTube demo feel, TTS voice A/B.
   Ask clearly; do not idle the rest of the DAG waiting.
3. On F2 checklist complete enough to ship: tag + push `v0.3.0-overhaul2`,
   rebuild installer if ISCC is present, tick PROGRESS F2/F3.

```powershell
git tag -a v0.3.0-overhaul2 -m "Overhaul 2 — YouTube, safety, computer use, advisor, goal-tree"
git push origin v0.3.0-overhaul2
# ISCC /DAppVersion=0.3.0 /DFlavor=cuda scripts\installer\polymath.iss
```

## Binaries
- CPU: `C:\pm\build\cpu\bin\Release\Polymath.exe`
- GPU: `C:\pm\build\cuda\bin\Polymath.exe`
