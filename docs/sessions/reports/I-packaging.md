# Wave 3 · Card I — Packaging, installer & first-run — report

**Status: PASS** (with one honest limit: no pristine VM available on this box —
clean-box install was *simulated* by extracting the zip to a fresh location and
running it, not by provisioning a new Windows image).

Owned `scripts/` + `docs/` only. No edits to `src/`, the GUI, or the FROZEN
contracts (`src/core/event_bus.h/.cpp`, `src/core/schema.h`). Built on the
previous agent's uncommitted progress (`first-run.ps1`, `check-gpu.ps1`, the
`package.ps1`/`fetch-models.ps1` edits, the partial `PACKAGING.md`) rather than
discarding it.

---

## What I verified (with real output)

### Build + tests — GREEN
- `pwsh scripts\build-cpu.ps1` — configure → build → ctest → windeployqt, exit 0.
- `ctest --test-dir build\cpu -C Release` — **10/10 pass**
  (`core, tools, audio, agent, vision, inference, memory, privacy, integration, ui`),
  `Total Test time 59.27 sec`. No new test added — this card owns `scripts/` +
  `docs/` (packaging is verified by exercising the bundle, not by a C++ unit test;
  the e2e suite from waves 1–2 stays green).

### Portable bundle — produced + validated
- `pwsh scripts\package.ps1 -Flavor cpu` → `dist\Polymath-0.1.0-win64-cpu.zip`
  (**96.1 MB** zip; staged folder **313 MB / 177 files**). The CUDA flavour is
  authored the same way (`-Flavor cuda`) but `build\cuda` is not present in this
  worktree (the GPU build needs the portable CUDA toolkit + a free GPU), so I
  packaged + validated the **CPU** flavour as the shippable artifact here.
- Bundle contents confirmed: `Polymath.exe`, full Qt runtime (`platforms\`,
  `qml\`, `imageformats\`, `lib\fonts\Inter.ttf`), engine/ONNX/OpenCV/fmt/spdlog
  DLLs, VC++ redist DLLs, the three first-run scripts, `Run-Polymath.cmd`,
  `README.txt`, and an **empty** `data\models\` + `PUT-MODELS-HERE.txt`. **No
  GGUFs bundled** (verified: `find data -name *.gguf` → none).

### Clean-box install — SIMULATED (honest limit)
I cannot provision a pristine VM on this machine. Next best, done for real:
1. **Extracted the zip to a fresh temp dir** (`%TEMP%\pm-cleanbox-test\…`, outside
   the build tree and the staging dir) — extracts to a single top-level folder,
   all scripts + empty `data\models\` present.
2. **Ran the bundle exe offscreen from the extracted location**
   (`QT_QPA_PLATFORM=offscreen`): the log shows `VramBudget … built without CUDA`,
   all 8 service threads start, `InferenceManager: no Fast model available at
   startup` is a **warning not a crash**, `AppController initialized`, and the
   process **stayed alive** until explicitly killed. This is the no-models path
   working: it guides, it does not die.
3. **Exercised the first-run wizard cold path** from the extracted bundle
   (`first-run.ps1 -NonInteractive -Choice skip -NoLaunch`): GPU check runs →
   model-set menu → "skip" prints the bring-your-own layout + the honest "chat/
   voice/agent stay disabled until you add a Fast LLM" warning. Also ran the
   `minimal`/`full` branches' dispatch (drives `fetch-models.ps1 -Minimal` / full)
   without actually re-downloading the 28 GB.

### Installer script — authored + dry-run validated
- **`scripts\installer\polymath.iss`** (Inno Setup) wraps the staged bundle into
  `dist\Polymath-<ver>-win64-<flavor>-Setup.exe`. **Inno Setup is NOT installed on
  this box** (`ISCC.exe` absent; NSIS absent too), so I could not compile the
  installer here. Per the card, I authored the script + documented the exact build
  command and verified the **portable zip is the shippable fallback**.
- Dry-ran the `.iss` logic: confirmed its `SourceDir`
  (`..\..\dist\Polymath-<ver>-win64-<flavor>`) resolves to the real staged folder,
  every file the `[Icons]`/`[Run]` sections reference exists in the bundle, and
  `powershell.exe` (its first-run launcher) is present in System32.

---

## What was broken (and fixed)

**The first-run scripts did not parse under Windows PowerShell 5.1** — the exact
shell a clean Windows box has (PowerShell 7 / `pwsh` is *not* preinstalled on
Windows, and both `Run-Polymath.cmd` and the installer invoke `powershell.exe`).
Root cause: the scripts were UTF-8 **without a BOM** and contained `U+2014` (em
dash) and `U+2026` (ellipsis). WinPS 5.1 decodes BOM-less files as the system ANSI
codepage, mangling those bytes and throwing parser errors (`Unexpected token`,
`Missing closing '}'`, `Missing type name after '['`). Under `pwsh` 7 (UTF-8 by
default) they worked, which is why the previous agent didn't catch it. **This
would have broken first-run on essentially every clean box.**

Fix: re-saved `check-gpu.ps1`, `first-run.ps1`, `fetch-models.ps1`, `package.ps1`
as **UTF-8 with BOM** (a BOM is honored by both 5.1 and 7, so the em-dashes
survive and the scripts parse everywhere). Verified all four now parse and run
correctly under **Windows PowerShell 5.1.19041** *and* pwsh 7.6.

---

## What I changed (files + why)

| File | Change |
|------|--------|
| `scripts\installer\polymath.iss` | **New.** Inno Setup script wrapping the staged bundle; per-user-capable install, Start-menu + optional desktop icons, post-install first-run/launch hook, keeps user models on uninstall, inline build + **code-signing** procedure. |
| `docs\PACKAGING.md` | **Finished** (was ~1.8 KB stub). Now covers: honest single-binary reality, producing the bundle, building + signing the installer, the full cold-start first-run flow, and the **models strategy** (Minimal ~3.5 GB vs Full ~28 GB table, the exact on-disk layout, and bring-your-own GGUF/ONNX guidance). |
| `scripts\package.ps1` | Launcher (`Run-Polymath.cmd`) now **prefers `pwsh`, falls back to `powershell`** (clean box has 5.1, not pwsh). Re-encoded UTF-8+BOM. (Prior agent's first-run-wizard wiring + README kept.) |
| `scripts\check-gpu.ps1`, `scripts\first-run.ps1`, `scripts\fetch-models.ps1` | Re-encoded **UTF-8 with BOM** so Windows PowerShell 5.1 parses them. `fetch-models.ps1`/`first-run.ps1`/`check-gpu.ps1` content (the `-Minimal` set, the wizard, the GPU verdict) is the prior agent's — reviewed, kept, and now verified end-to-end. |

---

## Residual gaps (what still needs a real clean VM / follow-up)

1. **Actual pristine-VM install not performed** — no VM provisioning on this box.
   The zip-extract-and-run simulation covers the app + first-run logic but **not**:
   (a) SmartScreen behaviour on an unsigned download, (b) a box with *no* VC++
   redist at all (we ship the redist DLLs beside the exe, which should cover it,
   but it's unverified on a truly bare image), (c) ISCC actually compiling the
   `.iss`. **Recommend a one-time clean Win10/11 VM pass** before shipping.
2. **Installer not compiled** — Inno Setup absent here. `install JRSoftware.InnoSetup`
   then run the documented `ISCC.exe /DAppVersion=… /DFlavor=… polymath.iss`.
3. **CUDA bundle not packaged this session** — only the CPU flavour was built in
   this worktree. `package.ps1 -Flavor cuda` is authored and the `.iss` takes
   `/DFlavor=cuda`; produce + validate the CUDA bundle on a box with the GPU build.
4. **Unsigned** — ships unsigned; signing procedure documented in both
   `PACKAGING.md` and the `.iss` header. Needs a real Authenticode (ideally EV)
   cert.
5. **ONNX `error 126` on load** — the CUDA-EP probe for
   `onnxruntime_providers_shared.dll` fails and falls back to CPU (that DLL is a
   GPU-package artifact the CPU ORT package doesn't ship). Pre-existing, documented
   in `docs\STATUS.md`, and harmless — perception runs on CPU by design in the CPU
   build. Not a bundle defect (the bundle ships the same `onnxruntime.dll` as the
   build tree).

## Contract requests

No new FROZEN-contract requests. One **forward-looking** note already implied by
this card and the F-gui requests: for a per-machine (Program Files) install where
users can't write under the install dir, `resolveAppRoot()` in `src\app\main.cpp`
should prefer `%LOCALAPPDATA%\Polymath`. Coded around here by defaulting the
installer to a writable location (`PrivilegesRequired=lowest`) so the portable
`data\`-beside-the-exe layout keeps working; noted in `PACKAGING.md §2`. This is a
`src\app` change outside this card's scope, not a frozen-contract edit, so no new
entry was needed in `contract-requests.md` (the existing F-gui `hasModels`/
`firstRun` entries already track the related app-facade surface).
