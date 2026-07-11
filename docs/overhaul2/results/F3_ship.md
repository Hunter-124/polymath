# F3 — Docs, graph, tag, installer

## Done
- Tag: `v0.3.0-overhaul2` (on master at F2/F3 live gate)
- Project `VERSION` bumped **0.1.0 → 0.3.0** (`CMakeLists.txt`)
- `STATUS.md` gates: CPU 22/22, CUDA green, F2 live, F3 installer
- `package.ps1`: robust `$PSScriptRoot` resolution (WinPS 5.1)
- Inno defaults: `AppVersion=0.3.0`

## Re-verify (2026-07-11 ship follow-up)
| Check | Result |
|-------|--------|
| CPU rebuild `Polymath` | PASS |
| `ctest -C Release -j1` | **22/22** |
| CUDA incremental rebuild (skill_registry + link) | PASS |
| `package.ps1 -Flavor cuda -NoZip` | PASS (~338 MB stage) |
| ISCC `/DAppVersion=0.3.0 /DFlavor=cuda` | PASS → `dist\Polymath-0.3.0-win64-cuda-Setup.exe` (~127 MB) |

## Artifacts (local, gitignored under `dist/`)
- `dist\Polymath-0.3.0-win64-cuda\`
- `dist\Polymath-0.3.0-win64-cuda-Setup.exe`

## Out of scope (Wave Z / residual)
- Code signing (SmartScreen unsigned warning)
- Clean-VM smoke
- Wave Z backlog items (SponsorBlock, AEC, new TTS engines, …)
