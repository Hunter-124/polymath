#include "retention.h"
#include "database.h"
#include "config.h"
#include "paths.h"
#include "types.h"
#include "logging.h"
#include <filesystem>

namespace polymath {

namespace {
constexpr int64_t kSecondsPerDay = 86'400;

// Remove a file that may be stored as a path relative to `base` or as an
// absolute path.  Silently ignores missing files, empty paths, and any path
// that would escape outside `base` (basic directory-traversal guard).
void removeMediaFile(const std::filesystem::path& base,
                     const std::string& rel) noexcept {
    if (rel.empty()) return;
    try {
        std::filesystem::path p(rel);
        // Resolve relative paths against the media root; keep absolute ones as-is.
        if (p.is_relative()) p = base / p;
        // Guard: canonical() throws if the file doesn't exist, so check first.
        if (!std::filesystem::exists(p)) return;
        // Reject anything outside `base` to prevent traversal attacks.
        auto canon  = std::filesystem::canonical(p);
        auto bcanon = std::filesystem::canonical(base);
        if (canon.string().find(bcanon.string()) != 0) return;
        std::filesystem::remove(canon);
    } catch (...) {
        // Missing files, permission errors, or bad paths — always safe to ignore.
    }
}
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
    // For each expired row collect on-disk paths before the DELETE so we can
    // remove the files.  clip_url is a camera-side SD card reference — never
    // touched here.  clip_local_path is a hub-archived copy — removed when set.
    const int events_days = cfg.getInt(keys::RetainEventsDays, 30);
    if (events_days > 0) {
        const int64_t cutoff = now - static_cast<int64_t>(events_days) * kSecondsPerDay;

        // Gather file paths before the rows are gone.
        const auto mediaRoot = Paths::instance().media();
        db_.query(
            "SELECT thumb_path, clip_local_path FROM events WHERE ts<?1", {cutoff},
            [&](const Row& r) {
                removeMediaFile(mediaRoot, r.text(0)); // thumb_path
                removeMediaFile(mediaRoot, r.text(1)); // clip_local_path (empty → no-op)
            });

        res.events_removed = countRows("events WHERE ts<?1", {cutoff});
        db_.exec("DELETE FROM events WHERE ts<?1", {cutoff});
    }

    // --- Measurements (instrument / voice / manual readings) ---------------
    // Shares the configured window; defaults to 0 (keep forever) when not set.
    const int meas_days = cfg.getInt(keys::RetainMeasurementsDays, 0);
    if (meas_days > 0) {
        const int64_t cutoff = now - static_cast<int64_t>(meas_days) * kSecondsPerDay;
        res.measurements_removed = countRows("measurements WHERE ts<?1", {cutoff});
        db_.exec("DELETE FROM measurements WHERE ts<?1", {cutoff});
    }

    if (res.total())
        PM_INFO("Retention.sweep: removed {} transcript(s), {} event(s), "
                "{} measurement(s) (ambient_days={}, events_days={}, meas_days={})",
                res.transcripts_removed, res.events_removed, res.measurements_removed,
                ambient_days, events_days, meas_days);
    return res;
}

} // namespace polymath
