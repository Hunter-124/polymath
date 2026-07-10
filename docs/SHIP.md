# Ship checklist — Polymath (Polymath engine)

Status as of the production-hardening pass. **Both flavors build, the full test suite is green, the
GPU build runs end-to-end with at-rest encryption active, and both installers compile.**

## What's done ✅

| Area | State |
|------|-------|
| CPU build | `pwsh scripts/build-cpu.ps1` green; `ctest --test-dir build/cpu -C Release` → **11/11** |
| GPU/CUDA build | `pwsh scripts/build-gpu.ps1` green (sm_86); `ctest --test-dir build/cuda -C Release` → **11/11** |
| GPU runtime | headless boot verified: encryption ACTIVE, `CUDA=true`, Fast model resident (gemma-3n-E4B, 36 layers, ~5.3 GB on GPU), 8 services up, stable |
| Voice | wake → VAD → whisper ASR → Utterance → SpeakRequest → Piper, with privacy gate |
| Agent | 17 tools' `invoke()` asserted + GBNF tool-call round-trip (grammar crash fixed) |
| Vision | person/motion/face/object-find on recorded clips (SCRFD decode bug fixed) |
| Inference | tiered Fast↔Heavy lifecycle, VRAM budget, no OOM; cross-thread CUDA fault fixed |
| Memory | vector recall, persistence, summarizer, retention |
| Privacy | master kill-switch + per-feature gating (live teardown), per-category retention, activity log |
| **At-rest encryption** | **ACTIVE** — vendored SQLCipher 4.6.1 + OpenSSL, AES; per-install DPAPI-protected key; plaintext→encrypted first-run migration; wrong-key open rejected |
| First-run UX | no-models wizard + GPU/driver check; live `hasModels`/`firstRun`, open-models-folder, add-model/role |
| UI | 10 themed views, bundled Inter font, empty/loading/error states (rendered headless to PNG) |
| Integration/CI | headless `AppController` harness + cross-service flows; `scripts/ci.ps1` (CPU, model-less green) |
| Phase 2 | ESP32-CAM ingest verified vs a software MJPEG stream; `browser_drive` CDP tool (real Chrome round-trip) |
| Mobile companion | `pm_gateway` embedded in `Polymath.exe` (LAN HTTP+WS on `:8765`, HMAC device tokens, shared `HttpRouter` for LAN + relay); `app/` PWA builds (`npm run build`→`app/dist/`); `cloud/relay/` builds (off by default). Desktop **Settings ▸ Mobile Access** mints a pairing QR (vendored MIT `qrcode.js`) + copyable payload. Runtime-verified: status→200, auth gate→401, clean start/stop. |
| Packaging | `scripts/package.ps1 -Flavor {cpu,cuda}` portable zips; **Inno Setup installers compile** for both flavors (silent install/launch/uninstall verified for CPU) |

## Release commands

```powershell
# CPU flavor
pwsh scripts/build-cpu.ps1
pwsh scripts/package.ps1 -Flavor cpu
& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" /DAppVersion=0.1.0 /DFlavor=cpu  scripts/installer/polymath.iss

# CUDA flavor (after the CPU prereqs exist)
pwsh scripts/build-gpu.ps1
pwsh scripts/package.ps1 -Flavor cuda
& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" /DAppVersion=0.1.0 /DFlavor=cuda scripts/installer/polymath.iss
# -> dist/Polymath-0.1.0-win64-{cpu,cuda}-Setup.exe
```

Models: minimal set (Fast + whisper + embeddings ≈ a few GB) vs full (~28 GB) — see
[`PACKAGING.md`](PACKAGING.md). Bundles ship **without** models; the first-run wizard fetches them.

## Residual gaps / honest TODOs ⏳

1. **Code signing.** Installers ship **unsigned** → SmartScreen warns on first download. Procedure is
   documented in `PACKAGING.md`; needs an Authenticode/EV cert.
2. **Clean-VM smoke.** Install→first-run→model-fetch was validated by simulation (extract-and-run,
   silent per-user install) on the dev box, **not** on a pristine Windows image. Do one clean Win10/11
   VM pass before a public release (SmartScreen, no-VC++-redist, ISCC signing).
3. **GPU perception.** YOLO/SCRFD/ArcFace run on **CPU** by design (we ship the CPU ONNX Runtime;
   `error 126` on `onnxruntime_providers_shared.dll` is the expected CPU fallback). Drop in the CUDA ORT
   package to accelerate; the code already requests the CUDA EP and falls back cleanly. llama/whisper are
   CUDA-accelerated.
4. **Heavy 27B on 12 GB.** Gemma 3 27B partial-offloads (VRAM budgeter trims layers) → correct but slow
   on a 12 GB card. A 12–14B Heavy fits far more.
5. **"Add GGUF…" file picker.** Opens the models folder (drop file + Refresh) because `QtQuick.Dialogs`
   isn't in the deployed Qt kit; `addModel(path, role)` is the live backing API. Add the module for a
   native picker.
6. **Optional Qt6::WebSockets** for `browser_drive` (currently a minimal RFC6455 client on `QTcpSocket`,
   fully working — no new dependency needed).
7. **Cosmetic.** Benign startup `UNIQUE constraint failed: models.id` warnings from model auto-register
   (registry ends correct); `windeployqt` "VCINSTALLDIR not set" warning (deploy still completes).
8. **License.** No `LICENSE` file yet — pick one before opening the repo widely (defaults to all-rights-reserved).

## First-run leftover

The plaintext→encrypted migration leaves a one-time `polymath.db.plaintext.bak` next to the DB (logged);
delete it once you've confirmed the encrypted DB opens.
