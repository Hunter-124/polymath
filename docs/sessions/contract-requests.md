# Contract change requests

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

## G-privacy — at-rest encryption needs a real SQLCipher codec
- Contract: build/toolchain (NOT a schema or event_bus edit)
- Proposed change: link a SQLCipher-enabled sqlite build instead of the plain
  vendored amalgamation (`third_party/sqlite3/sqlite3.c`). Either (a) compile the
  amalgamation with `SQLITE_HAS_CODEC` + the SQLCipher codec + OpenSSL, or
  (b) consume the existing `third_party/vcpkg/ports/sqlcipher` port (pulls
  openssl + tcl) and re-alias `unofficial::sqlite3::sqlite3` to it.
- Why: the encryption *wiring* is done (PRAGMA key + key check + codec detection
  in `database.cpp`), but plain SQLite silently ignores `PRAGMA key`, so the file
  on disk stays unencrypted. `Database::encryptionActive()` reports this honestly
  (it returns false here). Only a codec-enabled build can actually cipher the DB.
- Workaround used meanwhile: key path fully wired + verified; `open()` probes
  `PRAGMA cipher_version` and logs a clear warning when no codec is present; the
  e2e test branches on `encryptionActive()` and asserts the documented plaintext
  fallback so the gap is provable, not silent.

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
