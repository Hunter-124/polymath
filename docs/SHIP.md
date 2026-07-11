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

1. **Code signing execution.** Installers still ship **unsigned** until you supply a cert.
   Script ready: `scripts/sign-release.ps1` (supports `-DryRun`, `-Pfx`, `-Thumbprint`). Procedure in
   `PACKAGING.md` + `polymath.iss` header.
2. **Clean-VM smoke (pristine image).** Dev-box silent install script: `scripts/smoke-install.ps1`.
   Still recommend one pass on a clean Win10/11 VM before a public download page.
3. **GPU ORT redistributable (optional).** YOLO/SCRFD already request CUDA EP and fall back to CPU.
   Shipping the CUDA ORT package is optional packaging; llama/whisper remain CUDA-accelerated.
4. **Heavy 27B on 8 GB.** Partial-offload is correct but slow; prefer a smaller Heavy on this machine.

### Closed in Wave Z (v0.3.1)
- MIT `LICENSE` + `THIRD_PARTY_NOTICES.md`
- GGUF native picker (`pickAndAddModel`)
- models.id UNIQUE spam quieted
- `fs_write` undo journal + `fs_undo`
- `browser_drive` allowlist + block_file + session wipe
- SponsorBlock in `YtClean.js`
- Desktop Memory dashboard
- `calendar_read` / `inbox_notes` (local files only)
- identity.active_user_id for memory namespaces

## First-run leftover

The plaintext→encrypted migration leaves a one-time `polymath.db.plaintext.bak` next to the DB (logged);
delete it once you've confirmed the encrypted DB opens.
