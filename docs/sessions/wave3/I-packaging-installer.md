# Wave 3 · Card I — Packaging, installer & first-run

**Read [`../SHARED.md`](../SHARED.md) first.**  Owns: **`scripts/` + `docs/`** (+ a first-run
wizard — coordinate any QML with wave-2 F). Build on the existing `scripts/package.ps1` (already
produces a verified self-contained portable zip).

## Goal
Turn the portable zip into a real install + a smooth cold start.

## Do
1. **Installer** — wrap the bundle in an installer (Inno Setup / NSIS), or a signed portable.
   Document code-signing steps (cert) even if it ships unsigned for now.
2. **First-run UX** — on first launch with no models, run a model-fetch wizard (calls
   `fetch-models.ps1` with size/role guidance) + a GPU/driver check. Don't drop the user into a
   dead app that silently self-disables everything.
3. **Models strategy** — document the ~28 GB full set vs the minimal subset (Fast + whisper +
   embeddings ≈ a few GB), where each file goes, and how to bring your own GGUF/ONNX. Finish
   `docs/PACKAGING.md`.

## How
- Read `scripts/package.ps1`, `scripts/build-gpu.ps1`, `scripts/fetch-models.ps1`, and
  `docs/STATUS.md` / `docs/PACKAGING.md`.

## Done when
A clean Windows box: install → launch → guided model fetch → working assistant. Report at
`docs/sessions/reports/I-packaging.md`.
