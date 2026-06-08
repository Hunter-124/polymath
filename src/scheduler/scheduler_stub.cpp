#include "task_scheduler.h"
#include "proactive_engine.h"
#include "database.h"
#include "event_bus.h"
#include "logging.h"
#include "types.h"

// Wave-0 compiling stubs. Wave-2 scheduler agent implements the real queue
// draining (heavy-model load via InferenceManager), reminder evaluation,
// recurrence (rrule), presence/quiet-hours gating, and idle heuristics.

namespace polymath {

TaskScheduler::TaskScheduler(Database& db, InferenceManager& inf, QObject* parent)
    : QObject(parent), db_(db), inf_(inf) {}
void TaskScheduler::start() { PM_INFO("TaskScheduler started (stub)"); }
void TaskScheduler::stop() {}
qint64 TaskScheduler::enqueue(const std::string& type, const nlohmann::json& params, int priority) {
    auto now = to_unix(Clock::now());
    qint64 id = db_.exec(
        "INSERT INTO tasks(type,params_json,priority,status,created_at,updated_at)"
        " VALUES(?1,?2,?3,'queued',?4,?4)",
        {type, params.dump(), priority, now});
    EventBus::instance().publishTask({id, QString::fromStdString(type), "queued", ""});
    return id;
}
void TaskScheduler::cancel(qint64 id) {
    db_.exec("UPDATE tasks SET status='canceled' WHERE id=?1", {id});
}
void TaskScheduler::onIdleChanged(bool idle) { idle_ = idle; }

ProactiveEngine::ProactiveEngine(Database& db, QObject* parent) : QObject(parent), db_(db) {}
void ProactiveEngine::start() {
    PM_INFO("ProactiveEngine started (stub)");
    connect(&timer_, &QTimer::timeout, this, &ProactiveEngine::tick);
    timer_.start(30'000);
}
void ProactiveEngine::stop() { timer_.stop(); }
qint64 ProactiveEngine::addReminder(const std::string& text, qint64 due,
                                    const std::string& rrule, const std::string& cond) {
    return db_.exec("INSERT INTO reminders(text,due_at,rrule,condition,created_at)"
                    " VALUES(?1,?2,?3,?4,?5)",
                    {text, due, rrule, cond, to_unix(Clock::now())});
}
void ProactiveEngine::tick() { /* real impl: scan due reminders, fire via EventBus */ }
bool ProactiveEngine::inQuietHours() const { return false; }

IdleDetector::IdleDetector(QObject* parent) : QObject(parent) {}
void IdleDetector::start() {
    connect(&timer_, &QTimer::timeout, this, &IdleDetector::evaluate);
    timer_.start(10'000);
}
void IdleDetector::stop() { timer_.stop(); }
void IdleDetector::noteActivity() { last_activity_unix_ = to_unix(Clock::now()); }
void IdleDetector::evaluate() { /* real impl: flip idle_ after N quiet minutes */ }

} // namespace polymath
