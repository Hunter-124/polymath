#pragma once
//
// Retention — the data-layer retention sweeper.  Enforces per-category TTLs over
// the persisted tables: ambient transcripts (shortest by default), vision/audio
// events, and any row carrying an explicit ttl_at.  Pure SQL over the shared
// Database, so it is safe to run from any service thread (the Database serialises
// writes internally).
//
// MemoryService drives a live 24h cadence; this class is the canonical,
// unit-tested implementation of the policy and is also used directly by tools
// and tests.  Retention windows come from the `settings` table via Config
// (retention.ambient_days / retention.events_days; 0 == keep forever).
//
#include <cstdint>

namespace polymath {

class Database;

struct SweepResult {
    int64_t transcripts_removed = 0;
    int64_t events_removed      = 0;
    int64_t total() const { return transcripts_removed + events_removed; }
};

class Retention {
public:
    explicit Retention(Database& db) : db_(db) {}

    // Purges everything past its TTL as of `now_unix` (defaults to wall-clock):
    //   * transcripts with ttl_at <= now (explicit per-row TTL), and
    //   * ambient transcripts older than retention.ambient_days, and
    //   * events older than retention.events_days.
    // A category whose configured window is 0 is kept forever (except rows with
    // an explicit ttl_at, which are always honoured). Fresh rows are left intact.
    SweepResult sweep(int64_t now_unix = 0);

private:
    Database& db_;
};

} // namespace polymath
