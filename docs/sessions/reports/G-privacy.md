# Card G — Privacy, persistence & at-rest security — Report

**Status: PASS** (build green; `ctest -R privacy` green; full suite green).

> **Production-hardening update (at-rest encryption is now ACTIVE).** The
> toolchain gap below is **CLOSED**. The project now links a vendored
> **SQLCipher 4.6.1 amalgamation** (`third_party/sqlcipher/sqlite3.c`) compiled
> statically into `pm_core` with `SQLITE_HAS_CODEC=1`, `SQLITE_TEMP_STORE=2`,
> `SQLCIPHER_CRYPTO_OPENSSL=1`, linked against OpenSSL `libcrypto`. In a real run
> `Database::encryptionActive()` returns **true**, the on-disk `polymath.db` is
> **ciphertext** (no `SQLite format 3` header), and a wrong/missing key open is
> **rejected**. Key: a per-install random 256-bit secret in a **DPAPI-protected
> keyfile** (`<data>/db.key`); `AppController` loads it via
> `Database::loadOrCreateKey()` and opens with it. A pre-existing **plaintext**
> DB is **transparently migrated** to encrypted on first run via
> `sqlcipher_export()` (a `polymath.db.plaintext.bak` is kept). See the
> "At-rest encryption — ACTIVE" section and `contract-requests.md` (DONE).

At-rest-encryption verdict (original wave-2 state, now superseded): **WIRED, not
active — toolchain gap.** The key path (`PRAGMA key` + key verification + codec
detection) was fully wired in `database.cpp`, but the project linked the **plain
vendored SQLite amalgamation** (`third_party/sqlite3/sqlite3.c`), which silently
ignored `PRAGMA key`, so the file on disk was **not** ciphered.
`Database::encryptionActive()` reported this honestly. Linking SQLCipher was the
remaining dependency — now done.

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

## At-rest encryption — ACTIVE (production-hardening pass)
- **Approach:** vendored the **SQLCipher 4.6.1 amalgamation** (SQLite 3.46.1 base)
  at `third_party/sqlcipher/sqlite3.c`, generated from the official sqlcipher repo
  via `Makefile.msc`'s `sqlite3.c` target (`-DSQLITE_HAS_CODEC`). It is compiled
  **statically into `pm_core`** in `third_party/CMakeLists.txt` with
  `SQLITE_HAS_CODEC=1`, `SQLITE_TEMP_STORE=2`, `SQLCIPHER_CRYPTO_OPENSSL=1`, FTS5,
  threadsafe, linked to OpenSSL `libcrypto` (`openssl:x64-windows`, dynamic /MD to
  match the app CRT). Same aliased target `unofficial::sqlite3::sqlite3` → every
  module links it unchanged; ONE static code path for both the VS (build/cpu) and
  Ninja/CUDA (build/cuda) builds. Chosen over `vcpkg install sqlcipher` to keep the
  project's deliberate "no sqlite3.dll, single CRT" design (the vcpkg sqlite3 DLL
  had caused spurious SQLITE_NOMEM) and to avoid a tcl build-time dependency.
  Only one new runtime DLL: `libcrypto-3-x64.dll` (deployed by both build scripts).
  If OpenSSL is absent the CMake falls back to the plain amalgamation (encryption
  reported INACTIVE) so the build never breaks.
- **Key management:** a per-install random 256-bit secret generated with
  `BCryptGenRandom`, stored in `<data>/db.key` as a **DPAPI-protected blob**
  (`CryptProtectData`, current-user scoped), then run through
  `Database::deriveKey()` to produce the 64-hex SQLCipher key. Local-only, never
  hardcoded, off the command line, and only the current Windows user on this
  machine can unwrap it. `AppController::initialize()` calls
  `Database::loadOrCreateKey()` and opens with the key; if no key can be created it
  degrades to an unencrypted open (logged) rather than refusing to start.
- **Migration:** `Database::open()` detects a pre-existing **plaintext**
  `polymath.db` (standard `SQLite format 3\0` header) when a codec is linked and a
  key is supplied, and transparently re-encrypts it in place via
  `ATTACH … KEY` + `SELECT sqlcipher_export(...)`, preserving the original as
  `polymath.db.plaintext.bak` (the user deletes it once satisfied). No silent
  corruption / lock-out of an existing plaintext DB.
- **Verified:** `test_privacy_e2e.cpp` now REQUIRES the active path — sub-test [3]
  asserts `encryptionActive()`, ciphertext on disk (no plaintext, no SQLite
  header), and wrong-key + no-key rejection; new sub-test [3b] creates a plaintext
  DB, re-opens with a key, and asserts it was migrated to ciphertext with the row
  preserved and a `.plaintext.bak` kept.

## Residual gaps
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
