#include "memory_service.h"

#include "config.h"
#include "database.h"
#include "event_bus.h"
#include "inference_manager.h"
#include "logging.h"
#include "paths.h"
#include "summarizer.h"
#include "vector_index.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <mutex>
#include <unordered_map>

// MemoryService — long-term memory over SQLite (`memories`) + an hnswlib vector
// index for semantic recall, plus the daily summarizer and the retention
// sweeper. Lives on its own QThread (AppController). remember()/recall() are
// called from agent tool workers and only touch the (thread-safe) Database and
// the (internally mutex-guarded) VectorIndex, so they need no extra locking
// here. summarizeDay() is invoked by the scheduler as a deep task.

namespace polymath {

using nlohmann::json;

// ---------------------------------------------------------------------------
// Impl: owns the vector index + summarizer and the lazy-open bookkeeping.
// ---------------------------------------------------------------------------
struct MemoryService::Impl {
    VectorIndex index;
    std::unique_ptr<Summarizer> summarizer;

    std::once_flag open_flag;     // index opened exactly once, on first embed
    bool           index_ready = false;

    // Persist the index every N writes so a crash loses at most a few vectors.
    int            writes_since_save = 0;
    static constexpr int kSaveEvery = 16;
};

MemoryService::MemoryService(Database& db, InferenceManager& inf, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>()), db_(db), inf_(inf) {}

MemoryService::~MemoryService() = default;

void MemoryService::start() {
    PM_INFO("MemoryService started");
    d_->summarizer = std::make_unique<Summarizer>(db_, inf_);
    // The vector index is opened lazily on the first embedding (we need the
    // embedding model's output dimension, and that model may not be resident
    // yet at startup). recall()/remember() trigger ensureIndex() as needed.

    // Daily retention sweep. No other component owns a cadence for TTL
    // enforcement, so drive it here off this service's own event loop: the
    // sweep is self-contained (only touches the thread-safe Database) and safe
    // to run on the memory thread. Runs once at startup so a long-lived process
    // that just launched still enforces TTLs promptly, then every 24h.
    connect(&retention_timer_, &QTimer::timeout, this,
            &MemoryService::runRetentionSweep);
    retention_timer_.start(24 * 60 * 60 * 1000);   // 24h
    runRetentionSweep();
}

void MemoryService::stop() {
    retention_timer_.stop();
    if (d_->index_ready) {
        d_->index.save();
        PM_INFO("MemoryService: vector index persisted ({} vectors)", d_->index.size());
    }
    PM_INFO("MemoryService stopped");
}

// Embed `text` and, on the first successful embedding, open the on-disk vector
// index sized to the embedding dimension. Returns an empty Embedding if the
// embedding model is unavailable (caller then degrades to non-vector behavior).
Embedding MemoryService::embedText(const std::string& text) {
    Embedding vec = inf_.embed(text);
    if (vec.empty()) {
        PM_WARN("MemoryService: embedding unavailable (no embedding model loaded?)");
        return vec;
    }
    // Open the index once, now that we know the dimension.
    std::call_once(d_->open_flag, [this, dim = static_cast<int>(vec.size())] {
        const auto dir = Paths::instance().vectors();
        d_->index_ready = d_->index.open(dir, dim);
        if (d_->index_ready) {
            PM_INFO("MemoryService: vector index ready (dim={}, {} vectors) at {}",
                    dim, d_->index.size(), dir.string());
            backfillIndex();
        } else {
            PM_ERROR("MemoryService: failed to open vector index at {}", dir.string());
        }
    });
    return vec;
}

// On first open, re-index any memories that already have text but were stored
// without a vector (e.g. written by the Wave-0 stub or while no embedding model
// was loaded). Keeps recall consistent with the table.
void MemoryService::backfillIndex() {
    if (!d_->index_ready) return;

    struct Pending { int64_t id; std::string text; };
    std::vector<Pending> pending;
    db_.query("SELECT id,text FROM memories WHERE vector_id IS NULL AND text<>'' "
              "ORDER BY id ASC LIMIT 1000",
              {},
              [&](const Row& r) { pending.push_back({r.i64(0), r.text(1)}); });

    if (pending.empty()) return;
    PM_INFO("MemoryService: backfilling {} un-indexed memory row(s)", pending.size());

    int done = 0;
    for (const auto& p : pending) {
        Embedding vec = inf_.embed(p.text);     // index already open; don't recurse
        if (vec.empty()) break;                 // model went away mid-backfill
        if (d_->index.add(p.id, vec)) {
            db_.exec("UPDATE memories SET vector_id=?1 WHERE id=?2", {p.id, p.id});
            ++done;
        }
    }
    if (done) d_->index.save();
    PM_INFO("MemoryService: backfilled {}/{} memories", done, pending.size());
}

int64_t MemoryService::remember(const std::string& text, const std::string& kind, int64_t user) {
    if (text.empty()) {
        PM_WARN("MemoryService: remember() called with empty text");
        return -1;
    }

    const int64_t ts = to_unix(Clock::now());
    const json user_param = user < 0 ? json(nullptr) : json(user);

    // 1) Insert the row first so we have a stable id to use as the hnsw label.
    const int64_t id = db_.exec(
        "INSERT INTO memories(kind,text,vector_id,user_id,ts) VALUES(?1,?2,NULL,?3,?4)",
        {kind, text, user_param, ts});

    // 2) Embed + index. If embedding is unavailable we still keep the text row
    //    (vector_id stays NULL) and it gets indexed later by backfillIndex().
    Embedding vec = embedText(text);
    if (!vec.empty() && d_->index_ready) {
        if (d_->index.add(id, vec)) {
            // 3) Record the label so recall can join and so we don't re-backfill.
            db_.exec("UPDATE memories SET vector_id=?1 WHERE id=?2", {id, id});
            if (++d_->writes_since_save >= Impl::kSaveEvery) {
                d_->index.save();
                d_->writes_since_save = 0;
            }
        }
    }

    PM_INFO("MemoryService: remembered #{} kind='{}' ({} chars){}",
            id, kind, text.size(), vec.empty() ? " [text-only, not indexed]" : "");
    return id;
}

std::vector<MemoryHit> MemoryService::recall(const std::string& query, int k) {
    std::vector<MemoryHit> hits;
    if (query.empty() || k <= 0) return hits;

    Embedding qvec = embedText(query);
    if (qvec.empty() || !d_->index_ready) {
        PM_WARN("MemoryService: recall('{}') degraded — no vector index "
                "(embedding model unavailable)", query);
        return hits;   // no fallback ranking; an empty result is honest here
    }

    // Over-fetch so deletions / rows whose text changed don't shrink the result
    // below k after we join back to the table.
    const auto raw = d_->index.search(qvec, std::max(k, k * 2));
    if (raw.empty()) return hits;

    // Resolve labels -> current memory text in one IN(...) query, then re-order
    // to match the index's similarity ranking.
    std::string in_list;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (i) in_list += ',';
        in_list += std::to_string(raw[i].label);
    }

    std::unordered_map<int64_t, std::string> texts;
    db_.query("SELECT id,text FROM memories WHERE id IN (" + in_list + ")", {},
              [&](const Row& r) { texts.emplace(r.i64(0), r.text(1)); });

    for (const auto& hit : raw) {
        auto it = texts.find(hit.label);
        if (it == texts.end()) {
            // Stale label (memory deleted but still in the graph) — drop it and
            // tombstone it so future searches don't pay for it again.
            d_->index.remove(hit.label);
            continue;
        }
        hits.push_back({hit.label, it->second, hit.score});
        if (static_cast<int>(hits.size()) >= k) break;
    }

    PM_DEBUG("MemoryService: recall('{}') -> {} hit(s)", query, hits.size());
    return hits;
}

std::string MemoryService::summarizeDay(int64_t day_unix) {
    if (!d_->summarizer) {
        // summarizeDay may be invoked before start() in odd lifecycles; make it
        // self-sufficient rather than crashing.
        d_->summarizer = std::make_unique<Summarizer>(db_, inf_);
    }
    DaySummary s = d_->summarizer->summarizeDay(day_unix);

    // The summarizer inserts the memory row directly (it has the prose + source
    // tagging). Index that row too so the digest is itself semantically
    // recallable ("what did we do last Tuesday?").
    if (s.memory_id >= 0 && !s.text.empty()) {
        Embedding vec = embedText(s.text);
        if (!vec.empty() && d_->index_ready && d_->index.add(s.memory_id, vec)) {
            db_.exec("UPDATE memories SET vector_id=?1 WHERE id=?2",
                     {s.memory_id, s.memory_id});
            d_->index.save();
        }
    }
    return s.text;
}

void MemoryService::runRetentionSweep() {
    Config cfg(db_);
    const int64_t now = to_unix(Clock::now());

    // Database::exec() returns last_insert_rowid (not the affected-row count),
    // so we count matching rows with a COUNT(*) before deleting them.
    auto countRows = [this](const std::string& where,
                            const std::vector<json>& params) -> int64_t {
        int64_t n = 0;
        db_.query("SELECT COUNT(*) FROM " + where, params,
                  [&](const Row& r) { n = r.i64(0); });
        return n;
    };

    int64_t removed_transcripts = 0;
    int64_t removed_events      = 0;

    // --- Transcripts -------------------------------------------------------
    // Honor an explicit per-row ttl_at if present; otherwise fall back to the
    // configured ambient-retention window. retention.ambient_days == 0 => keep
    // forever (only ttl_at rows are swept).
    const int ambient_days = cfg.getInt(keys::RetainAmbientDays, 30);

    // 1) Rows whose explicit TTL has passed (set by the audio pipeline).
    removed_transcripts += countRows(
        "transcripts WHERE ttl_at IS NOT NULL AND ttl_at<=?1", {now});
    db_.exec("DELETE FROM transcripts WHERE ttl_at IS NOT NULL AND ttl_at<=?1", {now});

    // 2) Ambient rows older than the configured window (no explicit TTL).
    if (ambient_days > 0) {
        const int64_t cutoff = now - static_cast<int64_t>(ambient_days) * 86'400;
        removed_transcripts += countRows(
            "transcripts WHERE ttl_at IS NULL AND is_ambient=1 AND ts<?1", {cutoff});
        db_.exec(
            "DELETE FROM transcripts WHERE ttl_at IS NULL AND is_ambient=1 AND ts<?1",
            {cutoff});
    }

    // --- Events ------------------------------------------------------------
    // retention.events_days == 0 => keep forever.
    const int events_days = cfg.getInt(keys::RetainEventsDays, 90);
    if (events_days > 0) {
        const int64_t cutoff = now - static_cast<int64_t>(events_days) * 86'400;
        removed_events = countRows("events WHERE ts<?1", {cutoff});
        db_.exec("DELETE FROM events WHERE ts<?1", {cutoff});
    }

    if (removed_transcripts || removed_events) {
        PM_INFO("MemoryService: retention sweep removed {} transcript(s), {} event(s) "
                "(ambient_days={}, events_days={})",
                removed_transcripts, removed_events, ambient_days, events_days);
        EventBus::instance().publishNotice(
            {"info", "memory",
             "Retention sweep: removed " + std::to_string(removed_transcripts) +
                 " transcript(s) and " + std::to_string(removed_events) + " event(s)."});
    } else {
        PM_DEBUG("MemoryService: retention sweep — nothing past TTL");
    }
}

} // namespace polymath
