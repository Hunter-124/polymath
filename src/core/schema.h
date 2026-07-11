#pragma once
//
// Canonical SQLite schema (the shared data contract).  Applied idempotently by
// Database::migrate().  Bump kSchemaVersion and add ALTERs when evolving it.
// A human-readable copy lives in docs/SCHEMA.md.
//
namespace polymath {

// v3: scheduled_goals (overhaul2 D1 — timed/recurring agent goals).
// v4: goals.parent_id + join_policy (overhaul2 D2 — goal-tree orchestration).
//     Existing DBs pick up the columns via AgentLoop::ensureGoalTreeColumns()
//     (PRAGMA table_info + ALTER TABLE ADD COLUMN); CREATE below covers fresh DBs.
// v5: fs_undo_journal (Wave Z — content backup before fs_write overwrite).
inline constexpr int kSchemaVersion = 5;

inline constexpr const char* kSchemaSQL = R"SQL(
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

-- Inference model registry (Model Manager) -------------------------------
CREATE TABLE IF NOT EXISTS models (
    id            TEXT PRIMARY KEY,
    display_name  TEXT NOT NULL,
    path          TEXT NOT NULL,
    role          TEXT NOT NULL,            -- fast|heavy|vision|embedding
    n_ctx         INTEGER DEFAULT 4096,
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

-- Agent harness goals + plan steps (overhaul 03 §1) -----------------------
CREATE TABLE IF NOT EXISTS goals (
    id            INTEGER PRIMARY KEY,
    title         TEXT NOT NULL,
    status        TEXT NOT NULL DEFAULT 'active',
      -- active | waiting_user | waiting_agent | waiting_children | done | failed | cancelled
    origin        TEXT NOT NULL DEFAULT 'chat',
      -- chat | voice | schedule | skill | agent
    context_json  TEXT NOT NULL DEFAULT '{}',
    result_json   TEXT,
    created_at    INTEGER,
    updated_at    INTEGER,
    -- D2 goal-tree: NULL/0 = root; children of a parent join via join_policy
    parent_id     INTEGER,                  -- REFERENCES goals(id); root when NULL
    join_policy   TEXT NOT NULL DEFAULT 'all'
      -- all | any | first_success (how a parent waiting_children resumes)
);
CREATE INDEX IF NOT EXISTS idx_goals_parent_id ON goals(parent_id);
CREATE TABLE IF NOT EXISTS plan_steps (
    id            INTEGER PRIMARY KEY,
    goal_id       INTEGER NOT NULL REFERENCES goals(id),
    idx           INTEGER NOT NULL,
    description   TEXT NOT NULL,
    kind          TEXT NOT NULL,
      -- tool | prompt | skill | agent_session | surface
    tool          TEXT,
    args_json     TEXT,
    status        TEXT NOT NULL DEFAULT 'pending',
      -- pending | running | done | failed | skipped
    result_json   TEXT,
    attempts      INTEGER NOT NULL DEFAULT 0,
    updated_at    INTEGER
);

-- Scheduler v2: timed/recurring agent goals (overhaul2 D1) ----------------
-- ProactiveEngine's existing 30s tick() also scans this table; a due row
-- creates a real `goals` row (origin='schedule') through the same A2
-- execution path as run_skill, then reschedules per `kind`.
CREATE TABLE IF NOT EXISTS scheduled_goals (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    title         TEXT NOT NULL,
    prompt        TEXT DEFAULT '',        -- free-form instruction (kind="prompt" step)
    skill         TEXT DEFAULT '',        -- OR a registered skill name (kind="skill" step)
    params_json   TEXT NOT NULL DEFAULT '{}',
    kind          TEXT NOT NULL DEFAULT 'at',
      -- at (one-shot) | every (fixed interval) | rrule (iCal recurrence subset)
    spec          TEXT NOT NULL DEFAULT '',
      -- at: unix seconds (text); every: interval seconds (text); rrule: RRULE string
    next_fire     INTEGER,                -- unix; NULL => not scheduled (disabled/fired one-shot)
    last_fire     INTEGER,
    enabled       INTEGER NOT NULL DEFAULT 1,
    deliver       TEXT NOT NULL DEFAULT 'chat',
      -- chat (notify+transcript) | voice (also spoken) | notify (bypasses quiet hours)
    source        TEXT NOT NULL DEFAULT 'user',   -- who/what created this row
    created_at    INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_scheduled_goals_next_fire ON scheduled_goals(next_fire);

-- Wave Z: pre-overwrite backups for fs_write (content-addressed under data/undo/fs/)
CREATE TABLE IF NOT EXISTS fs_undo_journal (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    path          TEXT NOT NULL,            -- absolute path that was overwritten
    backup_path   TEXT NOT NULL,            -- copy under data/undo/fs/
    bytes         INTEGER DEFAULT 0,
    ts            INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_fs_undo_path ON fs_undo_journal(path);
CREATE INDEX IF NOT EXISTS idx_fs_undo_ts ON fs_undo_journal(ts);
)SQL";

} // namespace polymath
