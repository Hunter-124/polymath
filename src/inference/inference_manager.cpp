#include "inference_manager.h"
#include "database.h"
#include "event_bus.h"
#include "logging.h"

// NOTE: Wave-0 stub. The Wave-1 inference agent replaces the bodies below with
// the real llama.cpp-backed implementation (LlamaBackend, VramBudget, GBNF
// grammar, tiered load/unload). Signatures here are the frozen contract.

namespace polymath {

InferenceManager::InferenceManager(Database& db, QObject* parent)
    : QObject(parent), db_(db) {}
InferenceManager::~InferenceManager() = default;

void InferenceManager::start() {
    PM_INFO("InferenceManager started (stub) — load llama.cpp backends here");
    reloadRegistry();
}
void InferenceManager::stop() { backends_.clear(); }

void InferenceManager::reloadRegistry() {
    // Real impl: SELECT * FROM models; build ModelSpec list; load Fast (is_active).
}
std::vector<ModelSpec> InferenceManager::registry() const { return {}; }
void InferenceManager::setActiveModel(ModelRole, const std::string&) {}

void InferenceManager::generate(const ChatRequest& req) {
    // Stub echo so the rest of the pipeline can be wired/tested before the
    // backend lands. Real impl streams llama.cpp tokens.
    auto& bus = EventBus::instance();
    bus.publishToken({QString::fromStdString(req.request_id),
                      QStringLiteral("[inference not yet implemented]"), true});
}

Embedding   InferenceManager::embed(const std::string&) { return {}; }
std::string InferenceManager::describeImage(const Frame&, const std::string&) { return {}; }

void InferenceManager::requestHeavy(bool on) { PM_DEBUG("requestHeavy({})", on); }
bool InferenceManager::heavyLoaded() const { return false; }

} // namespace polymath
