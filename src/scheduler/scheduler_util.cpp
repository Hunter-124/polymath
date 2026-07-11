#include "scheduler_util.h"
#include "inference_manager.h"
#include "logging.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>

namespace polymath {
namespace sched_util {

int parseHhMm(const std::string& hhmm) {
    auto colon = hhmm.find(':');
    if (colon == std::string::npos) return -1;
    try {
        int h = std::stoi(hhmm.substr(0, colon));
        int m = std::stoi(hhmm.substr(colon + 1));
        if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
        return h * 60 + m;
    } catch (...) {
        return -1;
    }
}

bool inWindow(int64_t unix_ts, int start_min, int end_min) {
    if (start_min < 0 || end_min < 0) return false;
    if (start_min == end_min) return false;             // empty window

    std::time_t tt = static_cast<std::time_t>(unix_ts);
    std::tm lt{};
#if defined(_WIN32)
    localtime_s(&lt, &tt);
#else
    localtime_r(&tt, &lt);
#endif
    const int now_min = lt.tm_hour * 60 + lt.tm_min;

    if (start_min < end_min)                            // same-day window
        return now_min >= start_min && now_min < end_min;
    // wraps past midnight (e.g. 22:00 -> 07:00)
    return now_min >= start_min || now_min < end_min;
}

namespace {

// Pull the integer value of KEY=<int> out of an RRULE string (uppercased).
// Returns def if the key is absent or unpar.
int rruleInt(const std::string& upper, const std::string& key, int def) {
    auto pos = upper.find(key + "=");
    if (pos == std::string::npos) return def;
    pos += key.size() + 1;
    std::string digits;
    while (pos < upper.size() && std::isdigit(static_cast<unsigned char>(upper[pos])))
        digits.push_back(upper[pos++]);
    if (digits.empty()) return def;
    try { return std::stoi(digits); } catch (...) { return def; }
}

std::string rruleStr(const std::string& upper, const std::string& key) {
    auto pos = upper.find(key + "=");
    if (pos == std::string::npos) return {};
    pos += key.size() + 1;
    std::string val;
    while (pos < upper.size() && upper[pos] != ';')
        val.push_back(upper[pos++]);
    return val;
}

// Advance `from_unix` by `days`/`months` in LOCAL calendar terms, preserving
// the local wall-clock hour/minute/second. DST-safe: tm_isdst=-1 tells mktime
// to re-derive the correct UTC offset for the *resulting* local date, so e.g.
// a daily 08:00 occurrence stays 08:00 local across a spring-forward/
// fall-back transition instead of silently becoming 07:00 or 09:00.
int64_t addLocalCalendar(int64_t from_unix, int days, int months) {
    std::time_t tt = static_cast<std::time_t>(from_unix);
    std::tm lt{};
#if defined(_WIN32)
    localtime_s(&lt, &tt);
#else
    localtime_r(&tt, &lt);
#endif
    lt.tm_mday += days;
    lt.tm_mon  += months;
    lt.tm_isdst = -1;   // let mktime resolve DST for the resulting date
    std::time_t adjusted = std::mktime(&lt);
    return static_cast<int64_t>(adjusted);
}

} // namespace

int64_t advanceRrule(const std::string& rrule, int64_t from_unix) {
    if (rrule.empty()) return 0;
    std::string upper;
    upper.reserve(rrule.size());
    for (char c : rrule) upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));

    const std::string freq = rruleStr(upper, "FREQ");
    const int interval = std::max(1, rruleInt(upper, "INTERVAL", 1));
    if (freq.empty()) {
        PM_WARN("advanceRrule: no FREQ in '{}'", rrule);
        return 0;
    }

    if (freq == "MINUTELY") return from_unix + 60LL * interval;
    if (freq == "HOURLY")   return from_unix + 3600LL * interval;
    if (freq == "DAILY")    return addLocalCalendar(from_unix, interval, 0);
    if (freq == "WEEKLY")   return addLocalCalendar(from_unix, 7 * interval, 0);
    if (freq == "MONTHLY")  return addLocalCalendar(from_unix, 0, interval);

    PM_WARN("advanceRrule: unsupported FREQ '{}'", freq);
    return 0;
}

int64_t advanceEvery(int64_t from_unix, int64_t interval_s) {
    if (interval_s <= 0) return 0;
    return from_unix + interval_s;
}

} // namespace sched_util

// --- StreamCollector --------------------------------------------------------

StreamCollector::StreamCollector(QObject* parent) : QObject(parent) {
    // DirectConnection: the slot runs in the inference worker's thread context
    // (the bus emits there). All touched state is guarded by mtx_, and we wake
    // the waiting scheduler thread via cv_.
    connect(&EventBus::instance(), &EventBus::tokenStreamed,
            this, &StreamCollector::onToken, Qt::DirectConnection);
}

void StreamCollector::onToken(const TokenChunk& chunk) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (active_request_id_.empty() || chunk.request_id.toStdString() != active_request_id_)
        return;                                           // not our request
    buffer_ += chunk.text.toStdString();
    if (chunk.done) {
        done_ = true;
        cv_.notify_all();
    }
}

std::string StreamCollector::run(InferenceManager& inf, const ChatRequest& req,
                                 int timeout_ms, bool* ok) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        active_request_id_ = req.request_id;
        buffer_.clear();
        done_ = false;
    }

    inf.generate(req);

    std::unique_lock<std::mutex> lk(mtx_);
    const bool finished = cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                                       [this] { return done_; });
    std::string result = buffer_;
    active_request_id_.clear();
    if (ok) *ok = finished;
    if (!finished)
        PM_WARN("StreamCollector: timed out after {} ms for request '{}'", timeout_ms, req.request_id);
    return result;
}

} // namespace polymath
