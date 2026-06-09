# Contract change requests

> **Coordinator reconciliation — end of wave 2 (applied on master):**
> - ✅ *G — master kill-switch live propagation*: implemented in
>   `AppController::setPrivacy` (re-emits each sense key's effective value when
>   `privacy.master_enabled` flips).
> - ✅ *G — durable activity-log call-site*: `AgentRuntime` now calls
>   `ActivityLog::record(tool, summary, ok)` at the per-tool Notice site.
> - ✅ *H→E — `test_memory` model gate*: the hard `assert` on EmbeddingGemma is
>   now a clean SKIP when the model is absent (force with `POLYMATH_E2E_FULL=1`),
>   so CI is green on a model-less runner without the `ci.ps1` exclusion stopgap.
> - ⏳ *G — SQLCipher codec* and *F — extra AppController invokables
>   (`openModelsFolder`, `addModel`)*: DEFERRED (toolchain weight / non-blocking,
>   both coded around). Carry into wave 3 / a follow-up. `hasModels`/`firstRun`
>   properties also deferred; UI binds defensively today.


The frozen contracts — `src/core/event_bus.h` and `src/core/schema.h` — must not be edited
mid-wave (every service depends on their current shape). If a card needs a change, **append an
entry here instead** and code around it. A coordinator reconciles all requests in one pass at
the end of the wave.

Format:

```
## <card-id> — <short title>
- Contract: event_bus | schema
- Proposed change: <signal/field + type>
- Why: <what it unblocks>
- Workaround used meanwhile: <stub / local-only / deferred>
```

---

## G-privacy — at-rest encryption needs a real SQLCipher codec — ✅ DONE
- Contract: build/toolchain (NOT a schema or event_bus edit)
- **RESOLVED (production hardening pass):** chose approach (a) — vendored the
  **SQLCipher 4.6.1 amalgamation** (SQLite 3.46.1 base) under
  `third_party/sqlcipher/sqlite3.c`, compiled **statically into `pm_core`** with
  `SQLITE_HAS_CODEC=1`, `SQLITE_TEMP_STORE=2`, `SQLCIPHER_CRYPTO_OPENSSL=1`, and
  linked against OpenSSL `libcrypto` (`openssl:x64-windows`, dynamic /MD to match
  the app CRT). `third_party/CMakeLists.txt` swaps the sqlite3 source to the
  SQLCipher amalgamation when it is present + OpenSSL is found, keeping the SAME
  aliased target `unofficial::sqlite3::sqlite3` so every module links it
  unchanged — ONE static code path for both the VS (build/cpu) and Ninja/CUDA
  (build/cuda) builds. Falls back to the plain amalgamation if OpenSSL is absent.
  Chose static-vendored over `vcpkg install sqlcipher` to preserve the project's
  deliberate "no sqlite3.dll, one CRT" design (the vcpkg sqlite3 DLL had caused
  spurious SQLITE_NOMEM) and to avoid pulling tcl as a build-time dep.
- Result: `Database::encryptionActive()` returns **true** in a real run; the DB on
  disk is ciphertext (no `SQLite format 3` header); a wrong/missing key open is
  rejected (NOTADB). Key management: a per-install random 256-bit secret kept in a
  **DPAPI-protected keyfile** (`<data>/db.key`, `CryptProtectData`, current-user
  scoped), fed through `Database::deriveKey()` — local-only, never hardcoded.
  `AppController::initialize()` now calls `Database::loadOrCreateKey()` and opens
  with the key. MIGRATION: `Database::open()` detects a pre-existing plaintext DB
  and transparently re-encrypts it in place via `sqlcipher_export()` (keeping a
  `polymath.db.plaintext.bak`) so existing users are upgraded without lock-out.
  Verified by `ctest -R privacy` (encryption + migration sub-tests now REQUIRE the
  active ciphered path). Only runtime DLL added: `libcrypto-3-x64.dll` (deployed
  by both build scripts).

## G-privacy — master kill-switch live propagation (app-layer wiring)
- Contract: none frozen — integration note for src/app (out of card G's scope)
- Proposed change: in `AppController::setPrivacy`, when the key is
  `privacy.master_enabled`, also re-publish a `PrivacyChanged` for each per-feature
  sense key so already-running AudioService/VisionService re-apply capture state
  immediately (they only listen for their own feature keys today).
- Why: the master switch is enforced centrally at read time in `Config::getBool`
  (every service reads toggles through it, so a flip stops all *new* capture and
  all writes the moment a service re-reads). Services that cache the flag at
  start() won't tear down a *live* stream until they re-read without this re-emit.
- Workaround used meanwhile: kill-switch implemented + tested purely in
  `src/core` (Config gates the four sense toggles behind privacy.master_enabled);
  data-layer gate is proven by `ctest -R privacy`. Live teardown needs the 3-line
  app re-emit above.

## G-privacy — durable activity log wants its own table/category
- Contract: schema (deferred — coded around, no edit made)
- Proposed change: add an `activity_log(id, tool, summary, ok, ts)` table (or an
  `events.kind='tool'` blessed category) so web/tool actions have a first-class
  audit surface distinct from vision events.
- Why: card G requires web/tool actions be recorded and surfaced. The frozen
  schema has no activity table.
- Workaround used meanwhile: `ActivityLog::record()` (src/core/activity_log.*)
  persists actions into the existing `events` table with `kind='tool'`, which the
  Timeline/Privacy view already surfaces and the retention sweeper already ages
  out. Integration note: `AgentRuntime` should call `ActivityLog::record(tool,
  result.summary, result.ok)` where it already publishes the per-tool Notice
  (agent_runtime.cpp, ~line 334) so live tool calls are logged — a 2-line add in
  src/agent (out of card G's scope).

## F-gui — AppController surface gaps the views want — ✅ DONE
- Contract: none of the two FROZEN contracts. These were requests to the
  **AppController** facade (`src/app/app_controller.{h,cpp}`), which Card F may
  not edit (it owns only `src/ui/`).
- **RESOLVED (production hardening pass):** the three first-run affordances are
  now LIVE. All additions are small + additive; no frozen contract touched.
  - `Q_PROPERTY(bool hasModels READ hasModels NOTIFY modelsChanged)` —
    true when at least one registered `models`-table row's file still exists on
    disk (checked per-path so a deleted-file stale row doesn't mask cold start).
  - `Q_PROPERTY(bool firstRun READ firstRun NOTIFY firstRunChanged)` — true until
    the user has a usable model **OR** has acknowledged the first-run flow
    (persisted as the `app.first_run_done` setting; once acked it stays acked
    across restarts). Both NOTIFY signals are emitted from the existing
    `InferenceManager::modelStateChanged` hook in `wireEventBus()`, and from the
    actions below. The Dashboard cold-start banner now binds `app.hasModels`
    directly; the Privacy opt-in banner binds `app.firstRun` and gained a
    "Got it, continue" button → `app.completeFirstRun()`.
  - `Q_INVOKABLE void openModelsFolder()` — `QDesktopServices::openUrl` on
    `Paths::models()` (creates the dir first). Backs "Add GGUF…" + the empty-state
    "Open models folder" button.
  - `Q_INVOKABLE bool addModel(const QString& path, const QString& role)` —
    validates the file exists, inserts the `models` row exactly as auto-discover
    does (id == stem, all GPU layers, is_active only if the role is otherwise
    empty), queues `InferenceManager::reloadRegistry()`, emits `modelsChanged`.
    Returns false + posts a Notice on a bad path.
  - `Q_INVOKABLE void setModelRole(const QString& id, const QString& role)` —
    backs a per-row role ComboBox in ModelManagerView; UPDATEs the row's role,
    reloads the registry, emits `modelsChanged`. The view auto-refreshes via a
    `Connections { onModelsChanged }` block.
  - `Q_INVOKABLE void completeFirstRun()` — persists `app.first_run_done=1`.
- The UI render test (`tests/test_ui_e2e.cpp`) + screenshot harness
  (`src/ui/tools/capture_views.cpp`) stubs were extended to mirror the new
  surface so the full `ctest` stays green and every view still loads with zero
  QML errors. Defensive `!== undefined` guards on `hasModels`/`firstRun` removed
  now that the properties are real.

## J-phase2 — Qt6::WebSockets not in the kit (optional build/toolchain add)
- Contract: build/toolchain (NOT a schema or event_bus edit)
- Proposed change: add `qtwebsockets` to the aqtinstall module list in
  `scripts/build-cpu.ps1` (the `-m qtmultimedia qtimageformats qtshadertools`
  line) if a future card wants a fuller/WSS WebSocket surface.
- Why: the new `browser_drive` tool speaks the Chrome DevTools Protocol over a
  WebSocket, but `Qt6::WebSockets` is absent from the deployed Qt 6.6.3 kit (only
  `Qt6::Network` is installed). With the module present, the CDP transport could
  use `QWebSocket` instead of the hand-rolled RFC6455 client.
- Workaround used meanwhile: implemented a minimal RFC6455 client on `QTcpSocket`
  (already in `Qt6::Network`, already linked by `pm_agent`) — CDP is plain
  `ws://127.0.0.1:<port>` to localhost, so masked-client/unmasked-server text
  frames suffice. Zero new third-party deps. Fully working + tested
  (`ctest -R j_phase2`, B1 framing + B2 live Chrome round-trip). Non-blocking.
