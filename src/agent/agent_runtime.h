#pragma once
//
// AgentRuntime — thin owner of AgentLoop v2 (overhaul 03 §2 / C2).
//
// Wires EventBus / service entry points onto a worker thread, owns the tool
// registry + TurnCollector + AgentLoop, and serializes interactive turns.
// All plan/execute/reflect / context assembly lives in AgentLoop.
//
#include "service.h"
#include "tool_registry.h"
#include "event_bus.h"   // also provides types.h + Q_DECLARE_METATYPE for the
                          // payload types used in this class's slots (Utterance,
                          // AgentSessionEvent)
#include <QObject>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>
#include <string>

class QTimer;

namespace polymath {

class Database;
class InferenceManager;
class TaskScheduler;
class MemoryService;
class TurnCollector;
class AgentLoop;

class AgentRuntime : public QObject, public IService {
    Q_OBJECT
public:
    AgentRuntime(Database& db, InferenceManager& inf, TaskScheduler& sched,
                 MemoryService* memory = nullptr, QObject* parent = nullptr);
    ~AgentRuntime() override;

    void start() override;
    void stop() override;
    const char* serviceName() const override { return "agent"; }

    ToolRegistry& tools() { return registry_; }
    // Exposed for tests / scheduler resume hooks (null until start()).
    AgentLoop*    loop() { return loop_.get(); }

public slots:
    // Entry points. Both run the agentic loop on this service's worker thread.
    void handleUtterance(const polymath::Utterance& u);   // from ASR
    void handleTextInput(const QString& text, const QString& request_id); // from chat UI

    // A2: goal execution glue, all invoked on this worker thread (queued from
    // the tool that persisted the goal / from the EventBus / from a timer).
    //   executeGoalOne — run a specific goal, then drain any other runnable ones.
    //   kickResume     — resume every active goal with pending work (startup).
    //   onAgentSessionEvent — a finished/failed external session rejoins its goal.
    //   onJoinTimeoutTick   — reflect on goals parked waiting_agent too long.
    void executeGoalOne(qint64 goal_id);
    void kickResume();
    void onAgentSessionEvent(const polymath::AgentSessionEvent& e);
    void onJoinTimeoutTick();

signals:
    void turnStarted(QString request_id);
    void turnFinished(QString request_id, QString final_text);

private:
    // Core interactive path (runs on the agent worker thread).
    void runTurn(const std::string& user_text, const QString& request_id, bool from_voice);

    // Run `fn` on this worker thread while holding the single-turn busy_ guard.
    // If a turn/goal is already in flight (or a nested tool event loop is spinning
    // and busy_ is held), reschedule shortly instead of interleaving — the single
    // inference thread must run exactly one goal/turn at a time.
    void runGuarded(std::function<void()> fn);

    Database&                      db_;
    InferenceManager&              inf_;
    TaskScheduler&                 sched_;
    MemoryService*                 memory_ = nullptr;   // nullptr-safe
    ToolRegistry                   registry_;
    std::unique_ptr<TurnCollector> collector_;
    std::unique_ptr<AgentLoop>     loop_;
    // Periodic sweep for goals parked waiting_agent past agents.join_timeout_min.
    QTimer*                        join_timer_ = nullptr;
    // One turn at a time: tool dispatch spins a nested event loop (Qt Network),
    // so a queued second turn must not interleave with one in flight.
    std::atomic<bool>              busy_{false};
};

// Hand a freshly-persisted goal to the live AgentRuntime for execution (A2 §1,
// B-DEADGOAL). Thread-safe; a no-op (logged) when no runtime is running. The
// runtime executes the goal on its own worker thread via a queued call, so this
// is safe to call from inside a tool invocation (e.g. run_skill) — execution
// happens after the current turn returns to the event loop, never re-entrantly.
void requestGoalExecution(int64_t goal_id);

} // namespace polymath
