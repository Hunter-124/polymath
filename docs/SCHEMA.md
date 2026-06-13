# Hearth SQLite Schema

The **source of truth** is [`src/core/schema.h`](../src/core/schema.h)
(`kSchemaSQL` + `kColumnPatches`). `Database::migrate()` applies it
idempotently on every start. `kSchemaVersion` is currently **2** (2026-06,
device fabric).

---

## Tables

### `models` — inference model registry

| Column | Type | Notes |
|---|---|---|
| `id` | TEXT PK | model file identifier |
| `display_name` | TEXT | |
| `path` | TEXT | absolute path to the GGUF |
| `role` | TEXT | `fast\|heavy\|vision\|embedding` |
| `n_ctx` | INTEGER | default 8192 |
| `n_gpu_layers` | INTEGER | default 999 (offload all) |
| `chat_template` | TEXT | Jinja2 template string |
| `mmproj_path` | TEXT | vision projector path (VLM only) |
| `is_active` | INTEGER | 1 = currently loaded |

---

### `personalities` — modular persona bundles

| Column | Type | Notes |
|---|---|---|
| `name` | TEXT PK | |
| `bundle_path` | TEXT | `personalities/<name>/persona.json` |
| `voice` | TEXT | TTS voice id |
| `preferred_model` | TEXT | role hint (`fast`) |
| `wake_phrase` | TEXT | |
| `is_active` | INTEGER | |

---

### `tasks` — deep-work task queue

| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER PK AUTOINCREMENT | |
| `type` | TEXT | `lab_report\|research\|summary\|…` |
| `params_json` | TEXT | JSON blob |
| `priority` | INTEGER | higher = more urgent |
| `status` | TEXT | `queued\|running\|done\|error\|canceled` |
| `result_json` | TEXT | |
| `created_at` | INTEGER | unix seconds |
| `updated_at` | INTEGER | |

---

### `reminders` — proactive / scheduled notifications

| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER PK AUTOINCREMENT | |
| `text` | TEXT | |
| `due_at` | INTEGER | unix; NULL = condition-based |
| `rrule` | TEXT | optional recurrence rule |
| `condition` | TEXT | e.g. `someone_home` |
| `fired` | INTEGER | 1 once delivered |
| `created_at` | INTEGER | |

---

### `shopping_items`

| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER PK AUTOINCREMENT | |
| `item` | TEXT | |
| `quantity` | TEXT | |
| `done` | INTEGER | 0/1 |
| `created_at` | INTEGER | |

---

### `cameras` — registered camera sources

| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER PK AUTOINCREMENT | |
| `name` | TEXT | |
| `url` | TEXT | mjpeg/rtsp endpoint |
| `location` | TEXT | |
| `enabled` | INTEGER | |
| `device_id` | TEXT | *(v2)* FK → `edge_devices.id`; empty for legacy cameras |

---

### `events` — vision / audio events

| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER PK AUTOINCREMENT | |
| `kind` | TEXT | `motion\|person\|face\|sound` |
| `camera_id` | INTEGER | FK → `cameras.id` |
| `user_id` | INTEGER | FK → `users.id` (identified face) |
| `label` | TEXT | |
| `thumb_path` | TEXT | local thumbnail path |
| `ts` | INTEGER | unix seconds; indexed |
| `clip_url` | TEXT | *(v2)* edge-recorded clip URL (opaque; served from device SD) |
| `clip_local_path` | TEXT | *(v2)* hub-archived copy path (empty unless archival enabled) |
| `confidence` | REAL | *(v2)* on-device detector confidence 0–1 |
| `device_id` | TEXT | *(v2)* FK → `edge_devices.id` (originating edge device) |

Index: `idx_events_ts ON events(ts)`.

---

### `transcripts` — command and ambient speech

| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER PK AUTOINCREMENT | |
| `text` | TEXT | |
| `speaker` | INTEGER | FK → `users.id`; NULL = assistant |
| `is_ambient` | INTEGER | 0 = command, 1 = ambient |
| `ttl_at` | INTEGER | retention sweep deletes past this |
| `ts` | INTEGER | indexed |

---

### `users` — enrolled face / voice gallery

| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER PK AUTOINCREMENT | |
| `name` | TEXT | |
| `face_gallery` | TEXT | path to embeddings file |
| `voice_print` | TEXT | |
| `created_at` | INTEGER | |

---

### `memories` — long-term memory store

| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER PK AUTOINCREMENT | |
| `kind` | TEXT | `note\|fact\|summary\|caption` |
| `text` | TEXT | |
| `vector_id` | INTEGER | label in the hnswlib index |
| `source` | TEXT | |
| `user_id` | INTEGER | |
| `ts` | INTEGER | |

---

### `documents` — generated files

| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER PK AUTOINCREMENT | |
| `title` | TEXT | |
| `kind` | TEXT | `draft\|lab_report\|…` |
| `path` | TEXT | `documents/<file>.docx\|pdf` |
| `created_at` | INTEGER | |

---

### `settings` — key-value store

| Column | Type | Notes |
|---|---|---|
| `key` | TEXT PK | |
| `value` | TEXT | bool toggles use `"true"`/`"false"` |

---

## v2 Device fabric tables

Added in schema version 2 (2026-06). The fabric bridges autonomous edge
devices onto the hub's EventBus and schema without changing any upstream
consumers. See [`docs/FABRIC.md`](FABRIC.md) for the full wire contract.

---

### `edge_devices` — edge-device registry

One row per autonomous edge device (camera, voice satellite, instrument, panel).
**Distinct from the gateway's `devices` table** (which tracks paired
phones/web clients). Cameras may back-link here via `cameras.device_id`; legacy
cameras may have none.

| Column | Type | Notes |
|---|---|---|
| `id` | TEXT PK | stable id derived from MAC — `hearth-<kind3>-<hex6>`, e.g. `hearth-cam-a1b2c3` |
| `kind` | TEXT | `camera\|voice_sat\|instrument\|panel` |
| `name` | TEXT | |
| `location` | TEXT | |
| `transport` | TEXT | `mqtt\|http\|mjpeg\|rtsp` |
| `endpoint` | TEXT | base URL / MQTT topic root on the device |
| `capabilities` | TEXT | JSON object: per-kind feature flags |
| `fw_version` | TEXT | |
| `paired_key` | TEXT | per-device shared secret for direct mobile pairing |
| `last_seen` | INTEGER | unix; updated by MQTT birth/LWT and telemetry |
| `enabled` | INTEGER | |
| `created_at` | INTEGER | |

Index: `idx_edge_devices_kind ON edge_devices(kind)`.

---

### `instruments` — measurable channels

One row per measurable channel exposed by an HMM (lab module). Payload shape
intentionally matches Home-Assistant MQTT-discovery sensor fields so ESPHome
modules map 1:1.

| Column | Type | Notes |
|---|---|---|
| `id` | TEXT PK | unique channel id, e.g. `hmm_a1b2_balance_mass_g` |
| `device_id` | TEXT | FK → `edge_devices.id` (owning HMM module) |
| `name` | TEXT | |
| `channel` | INTEGER | channel index on multi-channel devices |
| `unit` | TEXT | `g`, `°C`, `hPa`, `pH`, … |
| `device_class` | TEXT | `mass\|temperature\|pressure\|ph\|co2\|voltage\|…` |
| `expected_min` | REAL | NULL = no range check |
| `expected_max` | REAL | NULL = no range check |
| `created_at` | INTEGER | |

---

### `measurements` — timestamped readings

Persisted by `FabricService::ingestReading()` (instrument push) or the
`record_measurement` agent tool (voice/manual entry).

| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER PK AUTOINCREMENT | |
| `instrument_id` | TEXT | FK → `instruments.id`; NULL for ad-hoc voice values |
| `session_id` | INTEGER | FK → `lab_sessions.id`; NULL outside a session |
| `value` | REAL | |
| `unit` | TEXT | |
| `in_range` | INTEGER | 1 if value is within `instruments.expected_min/max` |
| `source` | TEXT | `instrument\|voice\|manual` |
| `ts` | INTEGER | |

Indexes: `idx_measurements_ts ON measurements(ts)`,
`idx_measurements_session ON measurements(session_id)`.

---

### `lab_sessions` — guided lab-session state

Tracks an interactive lab session driven by the lab-report agent.

| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER PK AUTOINCREMENT | |
| `title` | TEXT | |
| `objective` | TEXT | |
| `status` | TEXT | `active\|paused\|done\|canceled` |
| `report_doc_id` | INTEGER | FK → `documents.id` once the report renders |
| `started_at` | INTEGER | |
| `ended_at` | INTEGER | NULL while active |

---

### `lab_session_steps` — per-step plan and captured values

| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER PK AUTOINCREMENT | |
| `session_id` | INTEGER NOT NULL | FK → `lab_sessions.id` |
| `step_no` | INTEGER | 1-based step order |
| `prompt` | TEXT | what the agent says/does at this step |
| `expected_kind` | TEXT | `temperature\|mass\|time\|ph\|volume\|…` |
| `expected_unit` | TEXT | |
| `expected_min` | REAL | |
| `expected_max` | REAL | |
| `measured_value` | REAL | captured reading |
| `measured_unit` | TEXT | |
| `verified` | INTEGER | 1 once captured and in-range |
| `verified_at` | INTEGER | |
| `note` | TEXT | |

Index: `idx_lab_steps_session ON lab_session_steps(session_id)`.

---

## Migration — idempotent column patches

`CREATE TABLE IF NOT EXISTS` makes new-table additions safe to re-run.
For columns added to **existing** tables, a bare `ALTER TABLE … ADD COLUMN`
is not idempotent (it errors if the column already exists). The migrator
therefore uses the `kColumnPatches` array in `schema.h`:

```cpp
struct ColumnPatch { const char* table; const char* column; const char* definition; };
inline constexpr ColumnPatch kColumnPatches[] = {
    // v2: edge-camera clip metadata + device back-links.
    { "events",  "clip_url",        "clip_url TEXT DEFAULT ''" },
    { "events",  "clip_local_path", "clip_local_path TEXT DEFAULT ''" },
    { "events",  "confidence",      "confidence REAL DEFAULT 0" },
    { "events",  "device_id",       "device_id TEXT DEFAULT ''" },
    { "cameras", "device_id",       "device_id TEXT DEFAULT ''" },
};
```

`Database::migrate()` calls `Database::hasColumn(table, column)` before each
entry and issues `ALTER TABLE … ADD COLUMN` only if the column is absent. This
makes the migrator safe to run against any database from schema v1 onward —
no version-gating required.
