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
                          // payload types used in this class's slots (Utterance)
#include <QObject>
#include <QString>
#include <atomic>
#include <memory>
#include <string>

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

signals:
    void turnStarted(QString request_id);
    void turnFinished(QString request_id, QString final_text);

private:
    // Core interactive path (runs on the agent worker thread).
    void runTurn(const std::string& user_text, const QString& request_id, bool from_voice);

    Database&                      db_;
    InferenceManager&              inf_;
    TaskScheduler&                 sched_;
    MemoryService*                 memory_ = nullptr;   // nullptr-safe
    ToolRegistry                   registry_;
    std::unique_ptr<TurnCollector> collector_;
    std::unique_ptr<AgentLoop>     loop_;
    // One turn at a time: tool dispatch spins a nested event loop (Qt Network),
    // so a queued second turn must not interleave with one in flight.
    std::atomic<bool>              busy_{false};
};

} // namespace polymath
