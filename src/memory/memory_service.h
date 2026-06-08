#pragma once
//
// MemoryService — long-term memory over SQLite (`memories`/`transcripts`) plus a
// vector index (hnswlib) for semantic recall.  Hosts the daily summarizer (run
// as a deep task) that turns the day's ambient transcript + events into next-day
// suggestions, reminders, and dinner-plan prompts.  Also runs the retention
// sweeper that enforces per-category TTLs.
//
#include "service.h"
#include "types.h"
#include <QObject>
#include <QTimer>
#include <memory>
#include <string>
#include <vector>

namespace polymath {

class Database;
class InferenceManager;

struct MemoryHit { int64_t id; std::string text; float score; };

class MemoryService : public QObject, public IService {
    Q_OBJECT
public:
    MemoryService(Database& db, InferenceManager& inf, QObject* parent = nullptr);
    ~MemoryService() override;

    void start() override;
    void stop() override;
    const char* serviceName() const override { return "memory"; }

    // Used by agent tools (remember/recall/search_memory).
    int64_t remember(const std::string& text, const std::string& kind = "note",
                     int64_t user_id = -1);
    std::vector<MemoryHit> recall(const std::string& query, int k = 5);

    // Invoked by the scheduler as the "summary" deep task.
    std::string summarizeDay(int64_t day_unix);
    void        runRetentionSweep();

private:
    // Embed `text`; on the first successful embedding, lazily open the vector
    // index sized to the embedding dimension. Empty result == no embedding model.
    Embedding embedText(const std::string& text);

    // One-time pass that indexes any memories rows still missing a vector_id
    // (e.g. written before an embedding model was available). Called from the
    // lazy index-open inside embedText().
    void backfillIndex();

    struct Impl;                       // hnswlib index + summarizer (vector_index/summarizer)
    std::unique_ptr<Impl> d_;
    Database&         db_;
    InferenceManager& inf_;

    // Daily TTL enforcement. Lives on the memory thread (created in start()),
    // so runRetentionSweep() touches the Database/index from the owning thread.
    QTimer retention_timer_;
};

} // namespace polymath
