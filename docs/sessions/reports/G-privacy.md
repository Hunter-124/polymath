# Card G — Privacy, persistence & at-rest security — Report

**Status: PASS** (build green; `ctest -R privacy` green; full suite 8/8 green).
At-rest-encryption verdict: **WIRED, not active — toolchain gap.** The key path
(`PRAGMA key` + key verification + codec detection) is fully wired in
`database.cpp`, but the project links the **plain vendored SQLite amalgamation**
(`third_party/sqlite3/sqlite3.c`), which silently ignores `PRAGMA key`, so the
file on disk is **not** ciphered. `Database::encryptionActive()` reports this
honestly. Linking SQLCipher is the remaining dependency (see residual gaps).

## How verified
- New integration test `tests/test_privacy_e2e.cpp`, registered as ctest
  `privacy` (links only `pm_core` + `Qt6::Core`). Runs on the CPU build against
  throwaway temp DBs.
- `ctest --test-dir build/cpu -C Release -R privacy` → **1/1 Passed (0.17s)**.
- Full suite `ctest -C Release` → **8/8 Passed** (core, tools, audio, agent,
  vision, inference, memory, privacy) — my changes break nothing.

### 1. Gating — master kill-switch + per-feature toggles — VERIFIED
- **Mechanism (in-scope, src/core):** added `privacy.master_enabled` (default ON)
  and made `Config::getBool` AND every per-feature *sense* toggle
  (mic / ambient transcription / face recognition / cameras) with the master
  switch. Every backend service already reads its toggle through `Config::getBool`
  / `Database::getBool` *before capturing or writing* (verified in
  `audio_service.cpp`, `vision_service.cpp`, `camera_worker.cpp`), so flipping the
  one master key makes all four senses read `false` with **no edits to the
  services** — capture and DB writes stop at the data-layer gate.
- Test asserts: defaults all ON; master OFF ⇒ all four senses read OFF while their
  *raw* stored value is unchanged (so the UI can still render the switch state);
  non-sense keys (encryption flag, retention windows) are **not** gated by master;
  an individual toggle still works independently with master back ON.
- **Holistic gating already solid from Wave 1** (re-verified by reading the code):
  mic OFF fully `capture.stop()`s the device (OS indicator clears) and ambient OFF
  drops to wake-only; cameras OFF stops every `CameraWorker` and clears visual
  memory; face-rec OFF is a live atomic the workers read per frame.
- **Live-flip caveat:** the master gate is enforced at *read* time, so it stops
  all new capture/writes the moment a service re-reads. Tearing down an
  *already-running* mic/camera stream the instant master flips needs a 3-line
  `AppController::setPrivacy` re-emit of the per-feature `PrivacyChanged` events
  (services only listen for their own feature keys today). That's `src/app`, out
  of card G's scope — filed in `contract-requests.md`.

### 2. Retention — per-category TTL purge — VERIFIED
- Added a canonical, unit-tested sweeper `Retention::sweep()`
  (`src/core/retention.{h,cpp}`): honours explicit per-row `transcripts.ttl_at`
  (set by the audio pipeline), then ages ambient transcripts past
  `retention.ambient_days` (shortest category) and events past
  `retention.events_days`; `*_days == 0` ⇒ keep forever (explicit-TTL rows still
  age). Returns a `SweepResult` with per-category counts.
- Test seeds a mix (explicit-TTL expired/fresh, ambient old/fresh, an old command
  transcript that is *kept*, old/recent events) and asserts: exactly the 2 stale
  transcripts + 1 stale event are removed, the 3 fresh/command rows + recent event
  survive, a second sweep is idempotent (0 removed), and `*_days=0` disables the
  window.
- Note: `MemoryService::runRetentionSweep()` (src/memory, out of scope) already
  drives a live 24h cadence with the same policy. The new `Retention` is the
  owned, tested reference implementation; consolidating the two is a residual
  cleanup (the memory copy is functionally equivalent).

### 3. At-rest encryption — WIRED (toolchain-limited) — VERIFIED as far as possible
- Rewrote `Database::open(path, key)`: when a key is supplied it runs
  `PRAGMA key='…'` (quote-escaped), probes `PRAGMA cipher_version` to detect a
  real codec, then forces a `SELECT count(*) FROM sqlite_master` so a wrong key
  against a SQLCipher file fails fast (`NOTADB` ⇒ `open()` returns false). Added
  `encryptionActive()` / `cipherVersion()` accessors and a dependency-free
  `Database::deriveKey()` (256-bit hex) so a passphrase need not sit on the
  command line.
- Test: keyed open writes a secret, re-opens **with** the key and round-trips it.
  It then branches on `encryptionActive()` — if SQLCipher is present it asserts
  the file is ciphered (no plaintext, no `SQLite format 3` header) and that a
  keyless open is rejected; on this toolchain it asserts the **documented**
  plaintext fallback (plain `SQLite format 3` header on disk) so the gap is
  **provable, not silently skipped**.
- Runtime confirmation from the test log:
  `Database: a key was supplied but the linked SQLite has no codec — the file is
  NOT encrypted (plain sqlite3).` and `encryptionActive=false cipher=''`.

### 4. Activity log — web/tool actions recorded + surfaced — VERIFIED
- Added `ActivityLog::record(tool, summary, ok)` (`src/core/activity_log.{h,cpp}`)
  which persists each web/tool action into the existing `events` table with
  `kind='tool'` (schema is frozen, so this reuses a free-text category). The
  Timeline/Privacy view already surfaces all `events` rows, and the retention
  sweeper already ages them out — so logged actions are both surfaced and bounded.
- Test records a `web_search`, a `fetch_page`, and a failed `print_document`,
  asserts all three land as `kind='tool'` events (with the failure flagged), and
  that they age out under `retention.events_days` like any other event.
- **Integration note:** the live recording call-site is `AgentRuntime` (src/agent,
  out of scope), which already emits a per-tool `Notice`; it should also call
  `ActivityLog::record(tool, result.summary, result.ok)` there (2 lines). Filed in
  `contract-requests.md`.

## Files changed (and why)
- `src/core/config.h` / `config.cpp` — master kill-switch key + `Config::getBool`
  gates the four sense toggles behind `privacy.master_enabled`; `masterEnabled()`,
  `isMasterGated()`, and a `respectMaster` opt-out for raw UI reads; seed default.
- `src/core/database.h` / `database.cpp` — real `PRAGMA key` wiring with key
  verification, SQLCipher codec detection (`encryptionActive`/`cipherVersion`),
  and `deriveKey()`. Replaces the previous fire-and-forget `PRAGMA key`.
- `src/core/retention.h` / `retention.cpp` — **new** owned, tested retention
  sweeper (`Retention::sweep` + `SweepResult`).
- `src/core/activity_log.h` / `activity_log.cpp` — **new** durable web/tool
  activity recorder over the `events` table.
- `src/core/CMakeLists.txt` — compile `retention.cpp` + `activity_log.cpp` into
  `pm_core`.
- `tests/test_privacy_e2e.cpp` — **new** e2e test (gating / retention / encryption
  / activity log); registered as ctest `privacy` in `tests/CMakeLists.txt`
  (append-only block).
- `docs/sessions/contract-requests.md` — 3 entries (SQLCipher toolchain, master
  live-propagation, activity-log table).

No frozen contracts touched: `src/core/event_bus.h/.cpp` and `src/core/schema.h`
are unchanged. No edits outside `src/core/`, `tests/`, and `docs/`.

## Residual gaps
1. **SQLCipher not in the build toolchain** (the at-rest-encryption gap). The
   wiring is complete and self-detecting, but the linked SQLite is the plain
   vendored amalgamation, so `PRAGMA key` is a no-op and the DB on disk is
   plaintext. A `sqlcipher` vcpkg port exists under `third_party/vcpkg/ports` but
   pulls openssl + tcl and would require re-aliasing
   `unofficial::sqlite3::sqlite3`. Once linked, `encryptionActive()` flips to true
   and the e2e test auto-exercises the ciphered-file + wrong-key-rejected path
   (no test change needed). Details in `contract-requests.md`.
2. **Master kill-switch live teardown** of an already-running stream needs the
   3-line `AppController::setPrivacy` re-emit (src/app). Read-time gating works
   today.
3. **Activity-log call-site**: `AgentRuntime` should call `ActivityLog::record`
   where it emits the per-tool Notice (src/agent, 2 lines). The recorder + its
   surfacing/retention are done and tested in core.
4. **Duplicate retention logic**: `MemoryService::runRetentionSweep` and the new
   `Retention::sweep` implement the same policy; the memory copy could delegate to
   the core class in a later pass.
5. **No UI master toggle**: `PrivacyView.qml` (src/ui) should add a
   `privacy.master_enabled` switch; the backing `app.privacy/setPrivacy` plumbing
   already handles it as a normal key.

## Contract requests
Three appended to `docs/sessions/contract-requests.md`: (a) SQLCipher-enabled
SQLite build (toolchain), (b) master kill-switch live propagation (app-layer),
(c) a first-class `activity_log` table/category (schema, coded around).
