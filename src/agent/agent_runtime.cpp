#include "agent_runtime.h"
#include "inference_manager.h"
#include "database.h"
#include "event_bus.h"
#include "logging.h"

// Wave-0 compiling stub. Wave-2 agent implements the real tool-calling loop
// (GBNF-constrained JSON, multi-step tool dispatch, deep-task queueing,
// persona system-prompt assembly, streaming + TTS hand-off).

namespace polymath {

AgentRuntime::AgentRuntime(Database& db, InferenceManager& inf, TaskScheduler& sched, QObject* parent)
    : QObject(parent), db_(db), inf_(inf), sched_(sched) {
    registerBuiltinTools(registry_);
}

void AgentRuntime::start() {
    PM_INFO("AgentRuntime started (stub): {} tools registered", registry_.names().size());
}
void AgentRuntime::stop() {}

void AgentRuntime::handleUtterance(const Utterance& u) {
    if (u.is_ambient) return;            // ambient text goes to memory, not the agent
    handleTextInput(QString::fromStdString(u.text), QStringLiteral("voice"));
}

void AgentRuntime::handleTextInput(const QString& text, const QString& request_id) {
    emit turnStarted(request_id);
    // Real impl: build ChatRequest with persona + tools, call inf_.generate(),
    // parse tool calls, dispatch via registry_, loop. Stub forwards to inference.
    ChatRequest req;
    req.request_id = request_id.toStdString();
    req.messages.push_back({Role::User, text.toStdString()});
    inf_.generate(req);
}

} // namespace polymath
