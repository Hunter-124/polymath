#include "agent_runtime.h"
#include "agent_loop.h"
#include "turn_collector.h"
#include "inference_manager.h"
#include "task_scheduler.h"
#include "logging.h"

// AgentRuntime — thin owner of AgentLoop v2.
//
// start() builds TurnCollector + AgentLoop on the worker thread, recovers any
// mid-flight plan_steps, and wires the tool registry into the scheduler for
// deep-task dispatch (A3). Interactive turns delegate entirely to AgentLoop.

namespace polymath {

AgentRuntime::AgentRuntime(Database& db, InferenceManager& inf, TaskScheduler& sched,
                           MemoryService* memory, QObject* parent)
    : QObject(parent), db_(db), inf_(inf), sched_(sched), memory_(memory) {
    registerBuiltinTools(registry_);
    // Thread tools + memory into the scheduler so deep-task drain can invoke
    // real ITools (e.g. generate_lab_report → .docx) and daily_summary.
    sched_.setToolRegistry(&registry_);
    sched_.setMemoryService(memory_);
}

AgentRuntime::~AgentRuntime() = default;

void AgentRuntime::start() {
    // Collector + loop must live on this service's thread (start() runs there).
    collector_ = std::make_unique<TurnCollector>();
    loop_ = std::make_unique<AgentLoop>(db_, inf_, sched_, registry_, memory_, *collector_);
    loop_->recoverOnStartup();
    PM_INFO("AgentRuntime started: {} tools registered (AgentLoop v2)",
            registry_.names().size());
}

void AgentRuntime::stop() {
    loop_.reset();
    collector_.reset();
}

// --- entry points -----------------------------------------------------------

void AgentRuntime::handleUtterance(const Utterance& u) {
    if (u.is_ambient) return;            // ambient text goes to memory, not the agent
    runTurn(u.text, QStringLiteral("voice"), /*from_voice*/ true);
}

void AgentRuntime::handleTextInput(const QString& text, const QString& request_id) {
    runTurn(text.toStdString(), request_id, /*from_voice*/ false);
}

// --- the loop ---------------------------------------------------------------

void AgentRuntime::runTurn(const std::string& user_text, const QString& request_id,
                           bool from_voice) {
    auto& bus = EventBus::instance();
    emit turnStarted(request_id);

    if (user_text.empty()) {
        emit turnFinished(request_id, QString());
        bus.publishToken({request_id, QString(), true});
        return;
    }
    if (!loop_ || !collector_) {
        PM_ERROR("AgentRuntime::runTurn: loop/collector not ready");
        emit turnFinished(request_id, QString());
        return;
    }

    // Serialize turns: tool dispatch spins a nested Qt event loop, so reject an
    // overlapping turn rather than let two interleave on this thread.
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) {
        PM_WARN("agent: busy with another turn; ignoring '{}'", request_id.toStdString());
        bus.publishNotice({"warn", "agent",
                           QStringLiteral("Still working on the previous request…")});
        return;
    }
    struct BusyGuard {
        std::atomic<bool>& b;
        ~BusyGuard() { b.store(false); }
    } guard{busy_};

    // Never let an exception escape this worker-thread slot: an uncaught throw
    // here would call std::terminate -> abort (a 0xC0000409 crash). Log it, tell
    // the UI the turn ended, and keep the app alive.
    try {
        const std::string answer = loop_->runInteractive(user_text, request_id, from_voice);
        emit turnFinished(request_id, QString::fromStdString(answer));
        PM_INFO("agent: turn '{}' finished ({} chars)",
                request_id.toStdString(), answer.size());
    } catch (const std::exception& e) {
        PM_ERROR("agent: turn '{}' aborted: {}", request_id.toStdString(), e.what());
        bus.publishNotice({"error", "agent",
                           QStringLiteral("Sorry — that request failed (%1).")
                               .arg(QString::fromStdString(e.what()))});
        bus.publishToken({request_id, QString(), true});   // unblock the UI stream
        emit turnFinished(request_id, QString());
    } catch (...) {
        PM_ERROR("agent: turn '{}' aborted: unknown error", request_id.toStdString());
        bus.publishToken({request_id, QString(), true});
        emit turnFinished(request_id, QString());
    }
}

} // namespace polymath
