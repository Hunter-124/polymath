#pragma once
//
// TaskScheduler — persistent priority queue of "deep-work" jobs (lab reports,
// research, daily summaries, batch image analysis).  Real-time voice bypasses
// it.  When the IdleDetector reports the machine quiet, it asks the
// InferenceManager to load the Heavy model, drains the queue, then releases it.
//
// A3: runTask dispatches known tool names via ITool::invoke, daily_summary via
// MemoryService::summarizeDay, else legacy completion.
//
#include "service.h"
#include <nlohmann/json.hpp>
#include <QObject>
#include <atomic>
#include <memory>
#include <optional>
#include <string>

namespace polymath {

class Database;
class InferenceManager;
class StreamCollector;
class ToolRegistry;
class MemoryService;

class TaskScheduler : public QObject, public IService {
    Q_OBJECT
public:
    TaskScheduler(Database& db, InferenceManager& inf, QObject* parent = nullptr);
    // Out-of-line so the unique_ptr<StreamCollector> (forward-declared above) is
    // destroyed in the .cpp where StreamCollector is complete — lets external
    // callers (tests, AppController) hold a TaskScheduler by value/own it.
    ~TaskScheduler() override;

    void start() override;
    void stop() override;
    const char* serviceName() const override { return "scheduler"; }

    // Wire tools + memory for deep-task dispatch (AgentRuntime sets these).
    // Nullptr-safe: missing tools/memory fall through to legacy completion.
    void setToolRegistry(ToolRegistry* tools) { tools_ = tools; }
    void setMemoryService(MemoryService* memory) { memory_ = memory; }

    // Push a deep task. Returns the task id (also persisted in `tasks`).
    qint64 enqueue(const std::string& type, const nlohmann::json& params, int priority = 0);
    void   cancel(qint64 task_id);

public slots:
    void onIdleChanged(bool idle);   // wired from IdleDetector

signals:
    void taskFinished(qint64 task_id, QString result_json);

private slots:
    // Runs on the scheduler thread (invoked via queued connection). Loads the
    // heavy model, pops queued tasks by priority and runs them, then releases.
    void drainQueue();

private:
    // One queued task pulled off the DB, ready to run.
    struct QueuedTask {
        qint64      id = 0;
        std::string type;
        std::string params_json;
        int         priority = 0;
    };

    std::optional<QueuedTask> popNextQueued();   // claim highest-priority task
    void                      runTask(const QueuedTask& t);

    Database&          db_;
    InferenceManager&  inf_;
    ToolRegistry*      tools_  = nullptr;          // optional: known-tool dispatch
    MemoryService*     memory_ = nullptr;          // optional: daily_summary
    std::atomic<bool>  idle_{false};               // read from enqueue() (other threads)
    bool               draining_ = false;          // re-entrancy guard (scheduler thread only)
    std::unique_ptr<StreamCollector> collector_;   // async->sync token bridge
};

} // namespace polymath
