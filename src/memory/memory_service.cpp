#include "memory_service.h"
#include "database.h"
#include "logging.h"

// Wave-0 compiling stub. Wave-2 memory agent implements Impl with the hnswlib
// index (vector_index.cpp), embedding via InferenceManager, the daily
// summarizer (summarizer.cpp), and the retention sweeper.

namespace polymath {

struct MemoryService::Impl { /* hnswlib index lives here */ };

MemoryService::MemoryService(Database& db, InferenceManager& inf, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>()), db_(db), inf_(inf) {}
MemoryService::~MemoryService() = default;

void MemoryService::start() { PM_INFO("MemoryService started (stub)"); }
void MemoryService::stop() {}

int64_t MemoryService::remember(const std::string& text, const std::string& kind, int64_t user) {
    return db_.exec("INSERT INTO memories(kind,text,user_id,ts) VALUES(?1,?2,?3,?4)",
                    {kind, text, user < 0 ? nlohmann::json(nullptr) : nlohmann::json(user),
                     to_unix(Clock::now())});
}
std::vector<MemoryHit> MemoryService::recall(const std::string&, int) { return {}; }
std::string MemoryService::summarizeDay(int64_t) { return {}; }
void MemoryService::runRetentionSweep() {}

} // namespace polymath
