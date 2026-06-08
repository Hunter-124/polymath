#include "summarizer.h"

#include "database.h"
#include "event_bus.h"
#include "inference_manager.h"
#include "logging.h"

#include <QObject>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

namespace polymath {

using nlohmann::json;

namespace {

// Cap how much material we feed the model so we never overrun the heavy model's
// context window. These are generous for a personal household; the prompt is
// truncated past them with a note.
constexpr int kMaxTranscriptLines = 400;
constexpr int kMaxEventLines      = 200;

// The framing for the summarizer turn. We ask for prose first, then a strict,
// machine-parseable JSON block we can pull follow-ups out of.
constexpr const char* kSystemPrompt =
    "You are Polymath, a thoughtful home assistant writing a private end-of-day "
    "digest for the household. You are given the day's ambient transcript lines "
    "and detected events. Write a short, friendly summary (a few short "
    "paragraphs or bullet points) of what notably happened. Be concrete, do not "
    "invent events that are not supported by the material, and never include "
    "personal data beyond what is provided.\n\n"
    "After the prose summary, output a fenced code block tagged `json` "
    "containing an object with two arrays:\n"
    "  \"suggestions\": up to 5 short strings — helpful ideas for tomorrow.\n"
    "  \"reminders\":   up to 5 objects {\"text\": string, \"time\": \"HH:MM\" or \"\"} "
    "for concrete things the household asked to be reminded of or clearly should "
    "be. Use \"\" for time when none is implied.\n"
    "If there is nothing worth suggesting or reminding, use empty arrays. Output "
    "the JSON block exactly once, at the very end.";

// Format a unix ts as local HH:MM for the prompt (compact, readable).
std::string hhmm(int64_t ts) {
    std::time_t tt = static_cast<std::time_t>(ts);
    std::tm lt{};
#if defined(_WIN32)
    localtime_s(&lt, &tt);
#else
    localtime_r(&tt, &lt);
#endif
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);
    return buf;
}

// Format a unix day-start as a local YYYY-MM-DD label.
std::string ymd(int64_t ts) {
    std::time_t tt = static_cast<std::time_t>(ts);
    std::tm lt{};
#if defined(_WIN32)
    localtime_s(&lt, &tt);
#else
    localtime_r(&tt, &lt);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    return buf;
}

// Extract the last ```json ... ``` (or ``` ... ```) fenced block from `text`.
// Returns the inner content, or "" if no fence is present.
std::string extractJsonBlock(const std::string& text) {
    const std::string fence = "```";
    // Find the last opening fence so trailing JSON wins over any earlier example.
    size_t open = text.rfind(fence);
    if (open == std::string::npos) return {};
    // The block we want is the *content between* the last two fences. rfind gave
    // us the closing fence; search backwards for the matching opener.
    size_t prev = text.rfind(fence, open == 0 ? 0 : open - 1);
    if (prev == std::string::npos || prev == open) return {};

    size_t content_start = prev + fence.size();
    // Skip an optional language tag (e.g. "json") + newline after the opener.
    size_t nl = text.find('\n', content_start);
    if (nl != std::string::npos && nl < open) content_start = nl + 1;

    if (content_start >= open) return {};
    return text.substr(content_start, open - content_start);
}

// Strip the trailing fenced JSON block (and its fences) from the prose so the
// stored digest is clean human text.
std::string stripJsonBlock(const std::string& text) {
    const std::string fence = "```";
    size_t close = text.rfind(fence);
    if (close == std::string::npos) return text;
    size_t open = text.rfind(fence, close == 0 ? 0 : close - 1);
    if (open == std::string::npos || open == close) return text;
    std::string prose = text.substr(0, open);
    // trim trailing whitespace/newlines
    while (!prose.empty() && (prose.back() == '\n' || prose.back() == '\r' ||
                              prose.back() == ' ' || prose.back() == '\t'))
        prose.pop_back();
    return prose;
}

// Parse "HH:MM" within the local day starting at `day_start` into a unix ts.
// Returns 0 when empty/unparseable (caller stores a condition-less reminder).
int64_t resolveDueUnix(const std::string& hhmm_str, int64_t day_start) {
    auto colon = hhmm_str.find(':');
    if (colon == std::string::npos) return 0;
    try {
        int h = std::stoi(hhmm_str.substr(0, colon));
        int m = std::stoi(hhmm_str.substr(colon + 1));
        if (h < 0 || h > 23 || m < 0 || m > 59) return 0;
        // Reminders extracted from "today" are intended for the next day.
        return day_start + 86'400 + (h * 3600 + m * 60);
    } catch (...) {
        return 0;
    }
}

// MemStreamCollector — async->sync bridge over EventBus::tokenStreamed, the same
// shape the scheduler's StreamCollector uses (which is private to that module,
// so we keep our own copy here). DirectConnection: onToken runs in the inference
// worker's thread; mtx_ guards the buffer and cv_ wakes our (blocked) caller.
class MemStreamCollector : public QObject {
public:
    MemStreamCollector() {
        connect(&EventBus::instance(), &EventBus::tokenStreamed,
                this, &MemStreamCollector::onToken, Qt::DirectConnection);
    }

    std::string run(InferenceManager& inf, const ChatRequest& req,
                    int timeout_ms, bool* ok) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            active_request_id_ = req.request_id;
            buffer_.clear();
            done_ = false;
        }
        inf.generate(req);

        std::unique_lock<std::mutex> lk(mtx_);
        const bool finished = cv_.wait_for(
            lk, std::chrono::milliseconds(timeout_ms), [this] { return done_; });
        std::string result = buffer_;
        active_request_id_.clear();
        if (ok) *ok = finished;
        if (!finished)
            PM_WARN("Summarizer: model stream timed out after {} ms (req '{}')",
                    timeout_ms, req.request_id);
        return result;
    }

private:
    void onToken(const TokenChunk& chunk) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (active_request_id_.empty() ||
            chunk.request_id.toStdString() != active_request_id_)
            return;
        buffer_ += chunk.text.toStdString();
        if (chunk.done) {
            done_ = true;
            cv_.notify_all();
        }
    }

    std::mutex              mtx_;
    std::condition_variable cv_;
    std::string             active_request_id_;
    std::string             buffer_;
    bool                    done_ = false;
};

} // namespace

Summarizer::Summarizer(Database& db, InferenceManager& inf) : db_(db), inf_(inf) {}

void Summarizer::localDayBounds(int64_t day_unix, int64_t& start, int64_t& end) {
    std::time_t tt = static_cast<std::time_t>(day_unix);
    std::tm lt{};
#if defined(_WIN32)
    localtime_s(&lt, &tt);
#else
    localtime_r(&tt, &lt);
#endif
    lt.tm_hour = 0;
    lt.tm_min  = 0;
    lt.tm_sec  = 0;
    lt.tm_isdst = -1;                    // let mktime resolve DST for this date
    start = static_cast<int64_t>(std::mktime(&lt));
    end   = start + 86'400;
}

std::vector<Summarizer::TranscriptLine>
Summarizer::loadTranscripts(int64_t start, int64_t end) const {
    std::vector<TranscriptLine> out;
    db_.query(
        "SELECT ts,is_ambient,text FROM transcripts WHERE ts>=?1 AND ts<?2 "
        "ORDER BY ts ASC LIMIT ?3",
        {start, end, kMaxTranscriptLines + 1},
        [&](const Row& r) {
            TranscriptLine l;
            l.ts      = r.i64(0);
            l.ambient = r.i64(1) != 0;
            l.text    = r.text(2);
            out.push_back(std::move(l));
        });
    return out;
}

std::vector<Summarizer::EventLine>
Summarizer::loadEvents(int64_t start, int64_t end) const {
    std::vector<EventLine> out;
    db_.query(
        "SELECT ts,kind,label FROM events WHERE ts>=?1 AND ts<?2 "
        "ORDER BY ts ASC LIMIT ?3",
        {start, end, kMaxEventLines + 1},
        [&](const Row& r) {
            EventLine e;
            e.ts    = r.i64(0);
            e.kind  = r.text(1);
            e.label = r.text(2);
            out.push_back(std::move(e));
        });
    return out;
}

std::string Summarizer::buildPrompt(int64_t start,
                                    const std::vector<TranscriptLine>& lines,
                                    const std::vector<EventLine>& events) const {
    std::string p;
    p += "Date: " + ymd(start) + "\n\n";

    p += "=== Detected events ===\n";
    if (events.empty()) {
        p += "(none)\n";
    } else {
        const size_t n = std::min<size_t>(events.size(), kMaxEventLines);
        for (size_t i = 0; i < n; ++i) {
            const auto& e = events[i];
            p += hhmm(e.ts) + "  " + e.kind;
            if (!e.label.empty()) p += " (" + e.label + ")";
            p += "\n";
        }
        if (events.size() > kMaxEventLines)
            p += "... (" + std::to_string(events.size() - kMaxEventLines) +
                 " more events omitted)\n";
    }

    p += "\n=== Transcript ===\n";
    if (lines.empty()) {
        p += "(no speech captured)\n";
    } else {
        const size_t n = std::min<size_t>(lines.size(), kMaxTranscriptLines);
        for (size_t i = 0; i < n; ++i) {
            const auto& l = lines[i];
            p += hhmm(l.ts) + (l.ambient ? "  [ambient] " : "  [command] ") + l.text + "\n";
        }
        if (lines.size() > kMaxTranscriptLines)
            p += "... (" + std::to_string(lines.size() - kMaxTranscriptLines) +
                 " more lines omitted)\n";
    }

    p += "\nWrite the digest now, then the JSON follow-ups block.";
    return p;
}

std::string Summarizer::runModel(const std::string& system, const std::string& user) const {
    ChatRequest req;
    req.model_id   = "";                        // active heavy model (scheduler loaded it)
    req.request_id = "memory-summary-" + std::to_string(to_unix(Clock::now()));
    req.sampling.max_tokens   = 1536;
    req.sampling.temperature  = 0.4f;           // factual, low-drift digest
    req.messages.push_back({Role::System, system});
    req.messages.push_back({Role::User,   user});

    MemStreamCollector collector;
    bool ok = false;
    std::string text = collector.run(inf_, req, /*timeout_ms=*/300'000, &ok);
    if (!ok)
        PM_WARN("Summarizer: model run did not finish cleanly; using partial output "
                "({} chars)", text.size());
    return text;
}

int64_t Summarizer::insertReminder(const std::string& text, int64_t due_unix) {
    json due_param = (due_unix > 0) ? json(static_cast<int64_t>(due_unix)) : json(nullptr);
    return db_.exec(
        "INSERT INTO reminders(text,due_at,rrule,condition,created_at) "
        "VALUES(?1,?2,'','',?3)",
        {text, due_param, to_unix(Clock::now())});
}

void Summarizer::persist(int64_t day_start, const std::string& model_output, DaySummary& out) {
    const std::string prose = stripJsonBlock(model_output);
    out.text = prose;

    // Store the prose digest as a long-term memory (kind='summary'). The vector
    // index is handled by MemoryService::remember(); here we only need the row,
    // so we insert directly with source tagging the day it covers.
    out.memory_id = db_.exec(
        "INSERT INTO memories(kind,text,vector_id,source,ts) "
        "VALUES('summary',?1,NULL,?2,?3)",
        {prose, "daily_summary:" + ymd(day_start), to_unix(Clock::now())});

    // Pull and parse the follow-ups block. Failures here are non-fatal: the
    // digest is already saved; we just skip reminders/suggestions.
    const std::string block = extractJsonBlock(model_output);
    if (block.empty()) {
        PM_INFO("Summarizer: no follow-ups JSON block in model output");
        return;
    }

    json j;
    try {
        j = json::parse(block);
    } catch (const std::exception& e) {
        PM_WARN("Summarizer: follow-ups JSON parse failed: {}", e.what());
        return;
    }

    // Suggestions: stored as their own lightweight memories so the proactive
    // engine / UI can surface them tomorrow. Capped defensively.
    if (j.contains("suggestions") && j["suggestions"].is_array()) {
        int n = 0;
        for (const auto& s : j["suggestions"]) {
            if (!s.is_string()) continue;
            const std::string text = s.get<std::string>();
            if (text.empty()) continue;
            db_.exec("INSERT INTO memories(kind,text,vector_id,source,ts) "
                     "VALUES('note',?1,NULL,?2,?3)",
                     {text, "suggestion:" + ymd(day_start), to_unix(Clock::now())});
            if (++n >= 5) break;
        }
        PM_INFO("Summarizer: stored {} suggestion(s)", n);
    }

    // Reminders: insert into the reminders table for the proactive engine.
    if (j.contains("reminders") && j["reminders"].is_array()) {
        int n = 0;
        for (const auto& r : j["reminders"]) {
            std::string text;
            std::string time;
            if (r.is_string()) {
                text = r.get<std::string>();
            } else if (r.is_object()) {
                if (r.contains("text") && r["text"].is_string())
                    text = r["text"].get<std::string>();
                if (r.contains("time") && r["time"].is_string())
                    time = r["time"].get<std::string>();
            }
            if (text.empty()) continue;
            const int64_t due = resolveDueUnix(time, day_start);
            const int64_t id  = insertReminder(text, due);
            out.reminder_ids.push_back(id);
            if (++n >= 5) break;
        }
        PM_INFO("Summarizer: inserted {} reminder(s)", n);
    }
}

DaySummary Summarizer::summarizeDay(int64_t day_unix) {
    DaySummary out;

    int64_t start = 0, end = 0;
    localDayBounds(day_unix, start, end);

    auto lines  = loadTranscripts(start, end);
    auto events = loadEvents(start, end);

    if (lines.empty() && events.empty()) {
        PM_INFO("Summarizer: nothing to summarize for {} (no transcripts/events)",
                ymd(start));
        return out;   // empty text == "no summary"
    }

    PM_INFO("Summarizer: summarizing {} ({} transcript lines, {} events)",
            ymd(start), lines.size(), events.size());

    const std::string prompt = buildPrompt(start, lines, events);
    const std::string output = runModel(kSystemPrompt, prompt);

    if (output.empty()) {
        PM_WARN("Summarizer: model returned no text for {}", ymd(start));
        return out;
    }

    persist(start, output, out);

    EventBus::instance().publishNotice(
        {"info", "memory",
         "Daily summary ready for " + ymd(start) + " (" +
             std::to_string(out.reminder_ids.size()) + " reminder(s))."});

    return out;
}

} // namespace polymath
