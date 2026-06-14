#pragma once
//
// Canonical SQLite schema (the shared data contract).  Applied idempotently by
// Database::migrate().  Bump kSchemaVersion and add ALTERs when evolving it.
// A human-readable copy lives in docs/SCHEMA.md.
//
namespace polymath {

// v2 (2026-06): distributed device fabric — devices/instruments/measurements/
// lab_sessions tables, edge-clip columns on events, device_id back-links.
// v3 (2026-06): local document RAG — knowledge_files/knowledge_chunks for the
// "ask your documents" capability (offline embedding + brute-force cosine).
inline constexpr int kSchemaVersion = 3;

inline constexpr const char* kSchemaSQL = R"SQL(
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

-- Inference model registry (Model Manager) -------------------------------
CREATE TABLE IF NOT EXISTS models (
    id            TEXT PRIMARY KEY,
    display_name  TEXT NOT NULL,
    path          TEXT NOT NULL,
    role          TEXT NOT NULL,            -- fast|heavy|vision|embedding
    n_ctx         INTEGER DEFAULT 8192,
    n_gpu_layers  INTEGER DEFAULT 999,
    chat_template TEXT DEFAULT '',
    mmproj_path   TEXT DEFAULT '',
    is_active     INTEGER DEFAULT 0
);

-- Modular personalities ---------------------------------------------------
CREATE TABLE IF NOT EXISTS personalities (
    name          TEXT PRIMARY KEY,
    bundle_path   TEXT NOT NULL,            -- personalities/<name>/persona.json
    voice         TEXT DEFAULT '',
    preferred_model TEXT DEFAULT 'fast',
    wake_phrase   TEXT DEFAULT '',
    is_active     INTEGER DEFAULT 0
);

-- Deep-work task queue ----------------------------------------------------
CREATE TABLE IF NOT EXISTS tasks (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    type        TEXT NOT NULL,              -- lab_report|research|summary|...
    params_json TEXT NOT NULL DEFAULT '{}',
    priority    INTEGER DEFAULT 0,
    status      TEXT DEFAULT 'queued',      -- queued|running|done|error|canceled
    result_json TEXT DEFAULT '',
    created_at  INTEGER NOT NULL,
    updated_at  INTEGER NOT NULL
);

-- Reminders / proactive ---------------------------------------------------
CREATE TABLE IF NOT EXISTS reminders (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    text        TEXT NOT NULL,
    due_at      INTEGER,                    -- unix; null => condition-based
    rrule       TEXT DEFAULT '',            -- optional recurrence
    condition   TEXT DEFAULT '',            -- e.g. 'someone_home'
    fired       INTEGER DEFAULT 0,
    created_at  INTEGER NOT NULL
);

-- Shopping list -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS shopping_items (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    item        TEXT NOT NULL,
    quantity    TEXT DEFAULT '',
    done        INTEGER DEFAULT 0,
    created_at  INTEGER NOT NULL
);

-- Cameras -----------------------------------------------------------------
CREATE TABLE IF NOT EXISTS cameras (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL,
    url         TEXT NOT NULL,              -- mjpeg/rtsp endpoint
    location    TEXT DEFAULT '',
    enabled     INTEGER DEFAULT 1
);

-- Vision/audio events -----------------------------------------------------
CREATE TABLE IF NOT EXISTS events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    kind        TEXT NOT NULL,              -- motion|person|face|sound
    camera_id   INTEGER,
    user_id     INTEGER,
    label       TEXT DEFAULT '',
    thumb_path  TEXT DEFAULT '',
    ts          INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_events_ts ON events(ts);

-- Transcripts (command + ambient) -----------------------------------------
CREATE TABLE IF NOT EXISTS transcripts (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    text        TEXT NOT NULL,
    speaker     INTEGER,
    is_ambient  INTEGER DEFAULT 0,
    ttl_at      INTEGER,                    -- retention sweep deletes past this
    ts          INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_transcripts_ts ON transcripts(ts);

-- Enrolled users (face/voice gallery) -------------------------------------
CREATE TABLE IF NOT EXISTS users (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL,
    face_gallery TEXT DEFAULT '',           -- path to embeddings file
    voice_print TEXT DEFAULT '',
    created_at  INTEGER NOT NULL
);

-- Long-term memory (text + vector id in the hnsw index) -------------------
CREATE TABLE IF NOT EXISTS memories (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    kind        TEXT DEFAULT 'note',        -- note|fact|summary|caption
    text        TEXT NOT NULL,
    vector_id   INTEGER,                    -- label in the hnswlib index
    source      TEXT DEFAULT '',
    user_id     INTEGER,
    ts          INTEGER NOT NULL
);

-- Generated documents (drafts / lab reports) ------------------------------
CREATE TABLE IF NOT EXISTS documents (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    title       TEXT NOT NULL,
    kind        TEXT DEFAULT 'draft',       -- draft|lab_report|...
    path        TEXT NOT NULL,              -- documents/<file>.docx|pdf
    created_at  INTEGER NOT NULL
);

-- Settings / privacy toggles / retention (key-value) ----------------------
CREATE TABLE IF NOT EXISTS settings (
    key         TEXT PRIMARY KEY,
    value       TEXT NOT NULL
);

-- =========================================================================
-- v2: distributed device fabric
-- =========================================================================

-- Edge-device registry — every autonomous edge device (camera/voice/instrument/
-- panel). NOTE: named `edge_devices`, NOT `devices` — the gateway (gateway_db.cpp)
-- owns a separate `devices` table for paired phones/web clients. `cameras` rows
-- back-link here via cameras.device_id; legacy cameras may have none.
CREATE TABLE IF NOT EXISTS edge_devices (
    id            TEXT PRIMARY KEY,          -- stable id (mac-derived), e.g. 'hearth-cam-a1b2c3'
    kind          TEXT NOT NULL,             -- camera|voice_sat|instrument|panel
    name          TEXT NOT NULL,
    location      TEXT DEFAULT '',
    transport     TEXT DEFAULT 'mqtt',       -- mqtt|http|mjpeg|rtsp
    endpoint      TEXT DEFAULT '',           -- base URL / mqtt topic root
    capabilities  TEXT DEFAULT '{}',         -- JSON: per-kind feature flags
    fw_version    TEXT DEFAULT '',
    paired_key    TEXT DEFAULT '',           -- per-device shared secret (direct mobile pair)
    last_seen     INTEGER DEFAULT 0,         -- unix; updated by birth/LWT + telemetry
    enabled       INTEGER DEFAULT 1,
    created_at    INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_edge_devices_kind ON edge_devices(kind);

-- Lab instruments — one row per measurable channel exposed by an HMM module.
CREATE TABLE IF NOT EXISTS instruments (
    id            TEXT PRIMARY KEY,          -- unique_id, e.g. 'hmm_a1b2_balance_mass_g'
    device_id     TEXT,                      -- FK edge_devices.id (owning HMM module)
    name          TEXT NOT NULL,
    channel       INTEGER DEFAULT 0,
    unit          TEXT DEFAULT '',           -- g, °C, hPa, pH, ...
    device_class  TEXT DEFAULT '',           -- mass|temperature|pressure|ph|co2|voltage|...
    expected_min  REAL,                      -- null => no range check
    expected_max  REAL,
    created_at    INTEGER NOT NULL
);

-- Measurements — timestamped readings (instrument-pushed or voice/manual-entered).
CREATE TABLE IF NOT EXISTS measurements (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    instrument_id TEXT,                      -- FK instruments.id (null for ad-hoc voice values)
    session_id    INTEGER,                   -- FK lab_sessions.id (null outside a session)
    value         REAL NOT NULL,
    unit          TEXT DEFAULT '',
    in_range      INTEGER DEFAULT 1,
    source        TEXT DEFAULT 'instrument', -- instrument|voice|manual
    ts            INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_measurements_ts ON measurements(ts);
CREATE INDEX IF NOT EXISTS idx_measurements_session ON measurements(session_id);

-- Interactive lab sessions — the state for the guided lab-report agent.
CREATE TABLE IF NOT EXISTS lab_sessions (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    title         TEXT NOT NULL,
    objective     TEXT DEFAULT '',
    status        TEXT DEFAULT 'active',     -- active|paused|done|canceled
    report_doc_id INTEGER,                   -- FK documents.id once the report renders
    started_at    INTEGER NOT NULL,
    ended_at      INTEGER
);

-- Per-step plan + captured/verified values for a lab session.
CREATE TABLE IF NOT EXISTS lab_session_steps (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id     INTEGER NOT NULL,
    step_no        INTEGER NOT NULL,
    prompt         TEXT DEFAULT '',          -- what the agent asks/does at this step
    expected_kind  TEXT DEFAULT '',          -- temperature|mass|time|ph|volume|...
    expected_unit  TEXT DEFAULT '',
    expected_min   REAL,
    expected_max   REAL,
    measured_value REAL,
    measured_unit  TEXT DEFAULT '',
    verified       INTEGER DEFAULT 0,        -- 1 once captured & in-range
    verified_at    INTEGER,
    note           TEXT DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_lab_steps_session ON lab_session_steps(session_id);

-- =========================================================================
-- v3: local document RAG ("ask your documents")
-- =========================================================================

-- Source files the user dropped into Paths::knowledge() for semantic search.
CREATE TABLE IF NOT EXISTS knowledge_files (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    path        TEXT UNIQUE NOT NULL,       -- absolute path on disk
    title       TEXT DEFAULT '',            -- filename (display)
    mtime       INTEGER DEFAULT 0,          -- file mtime when last indexed (skip-if-unchanged)
    chunk_count INTEGER DEFAULT 0,
    indexed_at  INTEGER NOT NULL
);

-- One embedded passage of a knowledge file. embedding = base64 of the unit
-- float32 vector; search is brute-force cosine (personal-scale corpora).
CREATE TABLE IF NOT EXISTS knowledge_chunks (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    file_id     INTEGER NOT NULL,
    chunk_no    INTEGER NOT NULL,
    text        TEXT NOT NULL,
    embedding   TEXT DEFAULT '',            -- base64 little-endian float32[dim]
    FOREIGN KEY(file_id) REFERENCES knowledge_files(id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_knowledge_chunks_file ON knowledge_chunks(file_id);
)SQL";

// Incremental column additions applied by Database::migrate() through a
// column-existence guard (bare ALTER ADD COLUMN is not idempotent). Each entry is
// {table, column-name, full column definition}.
struct ColumnPatch { const char* table; const char* column; const char* definition; };
inline constexpr ColumnPatch kColumnPatches[] = {
    // v2: edge-camera clip metadata + device back-links.
    { "events",  "clip_url",        "clip_url TEXT DEFAULT ''" },
    { "events",  "clip_local_path", "clip_local_path TEXT DEFAULT ''" },
    { "events",  "confidence",      "confidence REAL DEFAULT 0" },
    { "events",  "device_id",       "device_id TEXT DEFAULT ''" },
    { "cameras", "device_id",       "device_id TEXT DEFAULT ''" },
};

} // namespace polymath
