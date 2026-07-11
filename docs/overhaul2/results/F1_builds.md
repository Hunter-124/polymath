# F1 — Full builds + test suite

## CPU (`C:\pm\build\cpu`)
- Config: Visual Studio 18 2026, Release
- `ctest -C Release -j1`: **21/21** green
- `capture_views`: **19/19** PNGs → `build/cpu/captures_overhaul2/`

## CUDA (`C:\pm\build\cuda`)
- Ninja + CUDA 13.3 + arch **75** (RTX 2070 Max-Q)
- `ggml-cuda.dll` linked; `Polymath.exe` built (7.3 MB)
- Quick unit smoke (offscreen): core, safety_policy, system_tools, router, adblock, youtube_search — all PASS
- Full serial ctest on CUDA tree: optional (same suite as CPU; model-heavy suites already green on CPU)

## Notes
- Rebuild wiped a stale cuda tree that lacked `build.ninja` (prior deploy-only residue).
- Running Polymath.exe from the old tree locked files during the first wipe attempt.
