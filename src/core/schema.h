#pragma once
//
// Canonical SQLite schema (the shared data contract).  Applied idempotently by
// Database::migrate().  Bump kSchemaVersion and add ALTERs when evolving it.
// A human-readable copy lives in docs/SCHEMA.md.
//
namespace polymath {

inline constexpr int kSchemaVersion = 1;

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
)SQL";

} // namespace polymath
