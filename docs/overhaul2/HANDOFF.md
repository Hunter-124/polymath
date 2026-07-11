# Overhaul 2 — Resume handoff

## Done
All DAG nodes A1–A4, B1–B4, C1–C3, D1–D4, E1–E5, EV, F1 are landed on `master`.
- CPU: serial ctest **21/21**, capture_views **19/19**
- CUDA: `build/cuda/bin/Polymath.exe` + `ggml-cuda.dll` (arch 75)
- graphify updated under `graphify-out/`

## YOU ARE HERE — F2 owner sign-off
Open the GPU app and run the checklist in `docs/overhaul2/results/F2_e2e.md`.

**Required from owner:**
1. YouTube: "open a youtube video about castles" → native picker → click → ad-free play (×3 topics)
2. "slop mode" → video autoplays
3. TTS voice A/B: `af_heart` (default) vs `af_sky` — pick preferred default

Mark the owner boxes in F2_e2e.md, then:

```powershell
git tag -a v0.3.0-overhaul2 -m "Overhaul 2 — YouTube, safety, computer use, advisor, goal-tree"
git push origin v0.3.0-overhaul2
# Installer (if ISCC installed):
# ISCC /DAppVersion=0.3.0 /DFlavor=cuda scripts\installer\polymath.iss
```

## Binaries
- CPU: `C:\pm\build\cpu\bin\Release\Polymath.exe`
- GPU: `C:\pm\build\cuda\bin\Polymath.exe`
