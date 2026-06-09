#include "retention.h"
#include "database.h"
#include "config.h"
#include "types.h"
#include "logging.h"

namespace polymath {

namespace {
constexpr int64_t kSecondsPerDay = 86'400;
} // namespace

SweepResult Retention::sweep(int64_t now_unix) {
    const int64_t now = now_unix > 0 ? now_unix : to_unix(Clock::now());
    Config cfg(db_);

    // Database::exec() returns last_insert_rowid, not the affected-row count, so
    // count matches with COUNT(*) before each DELETE to report what we removed.
    auto countRows = [this](const std::string& where,
                            const std::vector<nlohmann::json>& params) -> int64_t {
        int64_t n = 0;
        db_.query("SELECT COUNT(*) FROM " + where, params,
                  [&](const Row& r) { n = r.i64(0); });
        return n;
    };

    SweepResult res;

    // --- Transcripts (ambient is the shortest-lived category) --------------
    // 1) Honour explicit per-row TTLs set by the capture pipeline regardless of
    //    the configured window (covers both ambient and command rows).
    res.transcripts_removed += countRows(
        "transcripts WHERE ttl_at IS NOT NULL AND ttl_at<=?1", {now});
    db_.exec("DELETE FROM transcripts WHERE ttl_at IS NOT NULL AND ttl_at<=?1", {now});

    // 2) Ambient rows without an explicit TTL fall back to retention.ambient_days.
    const int ambient_days = cfg.getInt(keys::RetainAmbientDays, 7);
    if (ambient_days > 0) {
        const int64_t cutoff = now - static_cast<int64_t>(ambient_days) * kSecondsPerDay;
        res.transcripts_removed += countRows(
            "transcripts WHERE ttl_at IS NULL AND is_ambient=1 AND ts<?1", {cutoff});
        db_.exec(
            "DELETE FROM transcripts WHERE ttl_at IS NULL AND is_ambient=1 AND ts<?1",
            {cutoff});
    }

    // --- Events (motion / person / face / tool activity) -------------------
    const int events_days = cfg.getInt(keys::RetainEventsDays, 30);
    if (events_days > 0) {
        const int64_t cutoff = now - static_cast<int64_t>(events_days) * kSecondsPerDay;
        res.events_removed = countRows("events WHERE ts<?1", {cutoff});
        db_.exec("DELETE FROM events WHERE ts<?1", {cutoff});
    }

    if (res.total())
        PM_INFO("Retention.sweep: removed {} transcript(s), {} event(s) "
                "(ambient_days={}, events_days={})",
                res.transcripts_removed, res.events_removed, ambient_days, events_days);
    return res;
}

} // namespace polymath
