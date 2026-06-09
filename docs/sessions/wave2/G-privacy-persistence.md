# Wave 2 · Card G — Privacy, persistence & at-rest security

**Read [`../SHARED.md`](../SHARED.md) first.**  Owns: **`src/core/` (db, settings, privacy) + the
retention sweeper.** Per-feature gating *checks* live in each service (wave 1 added the basic
toggle checks); you verify them holistically and own the data-layer security. Run after wave 1.

## Goal
Make "privacy default-ON but fully toggleable, local-only, encryptable" real and verified.

## Verify
1. **Gating actually stops capture** — master kill-switch + per-feature toggles (mic, ambient
   transcription, face ID, each camera): flip OFF → the matching pipeline produces nothing and
   writes nothing.
2. **Retention** — per-category TTLs (ambient transcripts shortest); the sweeper purges expired
   across all categories and leaves fresh data.
3. **At-rest encryption** — SQLCipher (or the chosen scheme) actually keys the DB: the file on
   disk is unreadable without the key, and the app opens it with the key. Confirm it's *wired*,
   not just linked.
4. **Activity log** — web/tool actions are recorded and surfaced (per the privacy/activity view).

## How
- Read `src/core/*` (db, settings, schema [read-only], any privacy/retention code) and how
  services query toggles before capturing. If gating needs a new EventBus/schema hook, file it in
  `contract-requests.md`.

## Done when
`ctest -R privacy` passes (toggle-gates-capture, retention purges, encrypted-open round-trip).
Report at `docs/sessions/reports/G-privacy.md`.
