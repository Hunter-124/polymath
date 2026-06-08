#pragma once
//
// Summarizer — turns one day's ambient transcripts + vision/audio events into a
// concise digest, persists it as a memory(kind='summary'), and extracts
// actionable follow-ups (next-day suggestions + reminders) which are written to
// the `reminders` table.
//
// Invoked by MemoryService::summarizeDay(), which the TaskScheduler runs as a
// "summary" deep task on its own worker thread *while the Heavy model is
// loaded*.  All work here is therefore synchronous/blocking by design: we issue
// a ChatRequest to the InferenceManager and block on the streamed result via an
// internal collector (the same async->sync bridge the scheduler uses).
//
#include "types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace polymath {

class Database;
class InferenceManager;

// Result of summarizing a single day.
struct DaySummary {
    std::string text;                       // the human-readable digest
    int64_t     memory_id = -1;             // memories.id of the stored summary (-1 if none)
    std::vector<int64_t> reminder_ids;      // reminders inserted from extracted follow-ups
};

class Summarizer {
public:
    Summarizer(Database& db, InferenceManager& inf);

    // Summarize the local day that contains `day_unix`. Reads that day's
    // transcripts + events, asks the model for (a) a prose digest and (b) a
    // small JSON block of follow-ups, stores the digest as a memory, inserts any
    // reminders, and returns everything. The summary text is empty on failure
    // (no material, or inference produced nothing).
    DaySummary summarizeDay(int64_t day_unix);

private:
    // A row pulled for the prompt context.
    struct TranscriptLine { int64_t ts; bool ambient; std::string text; };
    struct EventLine      { int64_t ts; std::string kind; std::string label; };

    // [start,end) unix bounds of the local calendar day containing `day_unix`.
    static void localDayBounds(int64_t day_unix, int64_t& start, int64_t& end);

    std::vector<TranscriptLine> loadTranscripts(int64_t start, int64_t end) const;
    std::vector<EventLine>      loadEvents(int64_t start, int64_t end) const;

    // Build the user prompt from the day's material.
    std::string buildPrompt(int64_t start,
                            const std::vector<TranscriptLine>& lines,
                            const std::vector<EventLine>& events) const;

    // Run the heavy model and return its full text (blocks on the stream).
    std::string runModel(const std::string& system, const std::string& user) const;

    // Pull the prose digest + the trailing ```json {suggestions,reminders}```
    // block out of the model output, persist them, and fill `out`.
    void persist(int64_t day_start, const std::string& model_output, DaySummary& out);

    // Insert one reminder (due_at NULL unless `due_unix` > 0). Returns its id.
    int64_t insertReminder(const std::string& text, int64_t due_unix);

    Database&         db_;
    InferenceManager& inf_;
};

} // namespace polymath
