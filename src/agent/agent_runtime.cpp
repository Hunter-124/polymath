#include "agent_runtime.h"
#include "agent_loop.h"
#include "turn_collector.h"
#include "inference_manager.h"
#include "task_scheduler.h"
#include "logging.h"

#include <QMetaObject>
#include <QTimer>
#include <atomic>

// AgentRuntime — thin owner of AgentLoop v2.
//
// start() builds TurnCollector + AgentLoop on the worker thread, recovers any
// mid-flight plan_steps, and wires the tool registry into the scheduler for
// deep-task dispatch (A3). Interactive turns delegate entirely to AgentLoop.

namespace polymath {

namespace {
// The single live AgentRuntime, so free-function tool hooks (run_skill →
// requestGoalExecution) can reach it without a hard dependency edge. Set on
// start(), cleared on stop(); atomic for the cross-thread publish.
std::atomic<AgentRuntime*> g_active_runtime{nullptr};
} // namespace

void requestGoalExecution(int64_t goal_id) {
    AgentRuntime* rt = g_active_runtime.load(std::memory_order_acquire);
    if (!rt) {
        PM_WARN("requestGoalExecution({}): no live AgentRuntime — goal parked", goal_id);
        return;
    }
    // Queued: runs on the runtime's worker thread after the current turn/tool
    // call returns to the event loop (never re-entrant).
    QMetaObject::invokeMethod(rt, "executeGoalOne", Qt::QueuedConnection,
                              Q_ARG(qint64, static_cast<qint64>(goal_id)));
}

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

    // Publish this runtime so run_skill (and other tools) can hand goals off for
    // execution (A2 §1 / B-DEADGOAL).
    g_active_runtime.store(this, std::memory_order_release);

    // Session rejoin (A2 §3): a finished/failed external agent session resumes
    // the goal parked waiting_agent on it. Queued automatically (the bus emits
    // from the sessions thread; this receiver lives on the agent worker thread).
    connect(&EventBus::instance(), &EventBus::agentSessionEvent,
            this, &AgentRuntime::onAgentSessionEvent);

    // Periodic join-timeout sweep so goals never hang forever on a dead session.
    join_timer_ = new QTimer(this);
    join_timer_->setInterval(60'000);   // 1 min; the timeout itself is minutes
    connect(join_timer_, &QTimer::timeout, this, &AgentRuntime::onJoinTimeoutTick);
    join_timer_->start();

    // Resume any active goals with pending work (A2 §2). Queued so start() can
    // return and the event loop spin before goals begin running.
    QMetaObject::invokeMethod(this, "kickResume", Qt::QueuedConnection);

    PM_INFO("AgentRuntime started: {} tools registered (AgentLoop v2)",
            registry_.names().size());
}

void AgentRuntime::stop() {
    g_active_runtime.store(nullptr, std::memory_order_release);
    if (join_timer_) { join_timer_->stop(); join_timer_ = nullptr; }
    loop_.reset();
    collector_.reset();
}

// --- goal execution glue (all on the agent worker thread) --------------------

void AgentRuntime::runGuarded(std::function<void()> fn) {
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) {
        // A turn/goal is in flight (possibly a nested tool event loop is spinning
        // and re-entered us). Retry shortly rather than interleave.
        QTimer::singleShot(200, this, [this, fn = std::move(fn)]() mutable {
            runGuarded(std::move(fn));
        });
        return;
    }
    struct BusyGuard {
        std::atomic<bool>& b;
        ~BusyGuard() { b.store(false); }
    } guard{busy_};
    try {
        fn();
    } catch (const std::exception& e) {
        PM_ERROR("agent: guarded goal task failed: {}", e.what());
    } catch (...) {
        PM_ERROR("agent: guarded goal task failed (unknown)");
    }
}

void AgentRuntime::executeGoalOne(qint64 goal_id) {
    if (!loop_) return;
    runGuarded([this, goal_id] {
        if (goal_id > 0) loop_->executeGoal(goal_id);
        // FIFO: after this goal reaches terminal/park, drain the next runnable
        // one(s) — including goals a run_skill call just created.
        loop_->resumeActiveGoals();
    });
}

void AgentRuntime::kickResume() {
    if (!loop_) return;
    runGuarded([this] { loop_->resumeActiveGoals(); });
}

void AgentRuntime::onAgentSessionEvent(const polymath::AgentSessionEvent& e) {
    if (!loop_) return;
    // Only terminal kinds resume a parked goal (Result = done, Error = failed).
    const QString kind = e.kind;
    if (kind != QLatin1String("Result") && kind != QLatin1String("Error")) return;
    const std::string sid  = e.session_id.toStdString();
    const std::string k    = kind.toStdString();
    const std::string text = e.text.toStdString();
    runGuarded([this, sid, k, text] {
        loop_->resumeForAgentSession(sid, k, text);
        loop_->resumeActiveGoals();   // pick up anything newly runnable
    });
}

void AgentRuntime::onJoinTimeoutTick() {
    if (!loop_) return;
    runGuarded([this] {
        loop_->sweepAgentJoinTimeouts();
        loop_->resumeActiveGoals();
    });
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
