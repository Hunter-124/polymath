#pragma once
//
// scheduler_util — small, dependency-light helpers shared by the scheduler
// module's three services. Internal to src/scheduler (NOT part of the frozen
// public contract). Covers:
//   * quiet-hours "HH:MM" parsing and the wrap-around midnight comparison,
//   * a minimal iCalendar RRULE advancer (the subset we actually emit),
//   * StreamCollector — turns InferenceManager's async token stream into a
//     blocking call usable from a scheduler worker thread.
//
#include "event_bus.h"
#include <QObject>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

namespace polymath {

class InferenceManager;

namespace sched_util {

// Parse "HH:MM" into minutes-since-midnight, or -1 if malformed.
int parseHhMm(const std::string& hhmm);

// Returns true if the given unix timestamp falls inside the [start,end) quiet
// window. `start`/`end` are minutes-since-midnight; the window wraps midnight
// when start > end (e.g. 22:00 -> 07:00). If either bound is invalid, false.
bool inWindow(int64_t unix_ts, int start_min, int end_min);

// Advance a unix timestamp by one step of the given RRULE, starting from
// `from_unix` (the previous due time). Supports a pragmatic subset:
//   FREQ=MINUTELY|HOURLY|DAILY|WEEKLY|MONTHLY  with optional INTERVAL=<n>.
// DAILY/WEEKLY/MONTHLY advance in LOCAL calendar terms (day-of-month / month
// arithmetic via mktime, tm_isdst=-1) so the local wall-clock hour:minute of
// `from_unix` is preserved across a DST transition (e.g. "daily at 08:00"
// stays 08:00 local, not drifting to 07:00/09:00 across spring-forward /
// fall-back). MINUTELY/HOURLY are plain elapsed-seconds arithmetic.
// Returns the next occurrence strictly after `from_unix`, or 0 if the rule is
// empty/unrecognised (caller should then treat the reminder as one-shot).
int64_t advanceRrule(const std::string& rrule, int64_t from_unix);

// Advance a unix timestamp by a plain elapsed-seconds interval (scheduled_goals
// kind="every"). Deliberately NOT calendar/DST-adjusted — an "every N seconds"
// cadence is literal wall-clock-elapsed time, unlike a daily/weekly RRULE.
// Returns from_unix + interval_s, or 0 if interval_s <= 0.
int64_t advanceEvery(int64_t from_unix, int64_t interval_s);

} // namespace sched_util

// StreamCollector — bridges InferenceManager::generate() (async, streams onto
// EventBus::tokenStreamed) into a synchronous "run and wait for the full
// answer" call. Lives on the scheduler worker thread; the bus delivers token
// chunks from the inference worker thread, so all shared state is mutex-guarded
// and we connect with a DirectConnection to avoid needing an event loop here.
class StreamCollector : public QObject {
    Q_OBJECT
public:
    explicit StreamCollector(QObject* parent = nullptr);

    // Issues `req` to `inf`, blocks until the matching request_id finishes (or
    // `timeout_ms` elapses), and returns the concatenated text. `ok` (optional)
    // is set false on timeout. Safe to reuse the same collector serially.
    std::string run(InferenceManager& inf, const ChatRequest& req,
                    int timeout_ms = 600'000, bool* ok = nullptr);

private:
    void onToken(const TokenChunk& chunk);

    std::mutex              mtx_;
    std::condition_variable cv_;
    std::string             active_request_id_;
    std::string             buffer_;
    bool                    done_ = false;
};

} // namespace polymath
