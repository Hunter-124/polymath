#include "task_scheduler.h"
#include "scheduler_util.h"
#include "inference_manager.h"
#include "database.h"
#include "event_bus.h"
#include "logging.h"
#include "types.h"
#include "i_tool.h"
#include "tool_registry.h"
#include "memory_service.h"

#include <QMetaObject>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

// TaskScheduler — persistent priority queue of deep-work jobs.
//
// Lifecycle: lives on its own QThread (see AppController). enqueue()/cancel()
// are called from other threads and only touch the (thread-safe) Database, so
// they need no extra locking. Draining happens entirely on the scheduler
// thread: onIdleChanged(true) hops onto this thread (queued connection) and
// runs drainQueue() inline — blocking work is exactly what this thread is for.
//
// A3 runTask dispatch order:
//   1. known tool name → ToolContext → ITool::invoke (e.g. generate_lab_report)
//   2. daily_summary   → MemoryService::summarizeDay
//   3. else            → legacy heavy-model completion

namespace polymath {

using nlohmann::json;

namespace {

// Compose the system prompt for a given deep-task type. The heavy model does
// the actual work; we just frame it. Unknown types get a generic worker prompt.
std::string systemPromptFor(const std::string& type) {
    if (type == "summary")
        return "You are a meticulous summarizer. Produce a concise, well-structured "
               "summary of the provided material. Use short paragraphs and bullet points.";
    if (type == "research")
        return "You are a research assistant. Investigate the question thoroughly and "
               "produce an organized briefing with key findings, caveats, and open questions.";
    if (type == "lab_report")
        return "You are a scientific writer. Produce a formal lab report with sections: "
               "Title, Objective, Materials, Method, Results, Discussion, Conclusion.";
    if (type == "daily_summary")
        return "You are a personal assistant. Summarize the day's notable events and "
               "transcripts into a short, friendly digest with any follow-ups.";
    if (type == "batch_image_analysis")
        return "You are a vision analyst. Summarize the supplied image observations into "
               "a structured report of notable people, objects, and events.";
    return "You are a careful deep-work assistant. Complete the requested task and "
           "return a thorough, well-structured result.";
}

// Build the user-facing instruction from the task params. We accept either a
// freeform {"prompt": "..."} or a {"topic"/"title"/"text": "..."} shape so
// callers (agent tools) can stay loose.
std::string userPromptFor(const std::string& type, const json& params) {
    auto pick = [&](const char* key) -> std::optional<std::string> {
        if (params.contains(key) && params[key].is_string())
            return params[key].get<std::string>();
        return std::nullopt;
    };
    if (auto p = pick("prompt")) return *p;

    std::string out;
    if (auto t = pick("title"))   out += "Title: " + *t + "\n";
    if (auto t = pick("topic"))   out += "Topic: " + *t + "\n";
    if (auto t = pick("question"))out += "Question: " + *t + "\n";
    if (auto t = pick("text"))    out += "Source material:\n" + *t + "\n";
    if (out.empty()) {
        // Fall back to dumping whatever params we got so nothing is silently lost.
        out = "Task type: " + type + "\nParameters:\n" + params.dump(2);
    }
    return out;
}

} // namespace

TaskScheduler::TaskScheduler(Database& db, InferenceManager& inf, QObject* parent)
    : QObject(parent), db_(db), inf_(inf) {}

// Defined here (not =default in the header) so ~unique_ptr<StreamCollector> sees
// the complete type — StreamCollector is only forward-declared in the header.
TaskScheduler::~TaskScheduler() = default;

void TaskScheduler::start() {
    PM_INFO("TaskScheduler started");
    // Recover any tasks that were left 'running' by a previous crash/shutdown.
    db_.exec("UPDATE tasks SET status='queued', updated_at=?1 WHERE status='running'",
             {to_unix(Clock::now())});
    // Startup recovery for plan_steps (03 §1): running → pending so goals resume.
    db_.exec("UPDATE plan_steps SET status='pending', updated_at=?1 WHERE status='running'",
             {to_unix(Clock::now())});
    collector_ = std::make_unique<StreamCollector>();   // bound to the bus on this thread
}

void TaskScheduler::stop() {
    draining_ = false;
    collector_.reset();
    PM_INFO("TaskScheduler stopped");
}

qint64 TaskScheduler::enqueue(const std::string& type, const nlohmann::json& params, int priority) {
    auto now = to_unix(Clock::now());
    qint64 id = db_.exec(
        "INSERT INTO tasks(type,params_json,priority,status,created_at,updated_at)"
        " VALUES(?1,?2,?3,'queued',?4,?4)",
        {type, params.dump(), priority, now});
    EventBus::instance().publishTask({id, QString::fromStdString(type), "queued", ""});
    PM_INFO("TaskScheduler: enqueued task {} type={} priority={}", id, type, priority);

    // If we're already idle, kick the drain (hops onto this thread).
    if (idle_)
        QMetaObject::invokeMethod(this, "drainQueue", Qt::QueuedConnection);
    return id;
}

void TaskScheduler::cancel(qint64 id) {
    db_.exec("UPDATE tasks SET status='canceled', updated_at=?2 WHERE id=?1 "
             "AND status IN ('queued','running')",
             {id, to_unix(Clock::now())});
    EventBus::instance().publishTask({id, "", "canceled", ""});
    PM_INFO("TaskScheduler: canceled task {}", id);
}

void TaskScheduler::onIdleChanged(bool idle) {
    idle_ = idle;
    if (idle)
        QMetaObject::invokeMethod(this, "drainQueue", Qt::QueuedConnection);
    // Note: once a drain is in flight this thread's event loop is busy running
    // the (blocking) task, so a later onIdleChanged(false) is only delivered
    // after the current task completes. We don't hard-abort an in-flight stream;
    // the while(idle_) check just stops us from starting the *next* task.
}

void TaskScheduler::drainQueue() {
    if (draining_) return;             // re-entrancy / double-kick guard
    if (!idle_) return;

    // Anything to do?
    int pending = 0;
    db_.query("SELECT COUNT(*) FROM tasks WHERE status='queued'", {},
              [&](const Row& r) { pending = static_cast<int>(r.i64(0)); });
    if (pending == 0) return;

    draining_ = true;
    PM_INFO("TaskScheduler: idle — draining {} queued task(s)", pending);
    EventBus::instance().publishNotice({"info", "scheduler",
        QString::fromStdString("Idle detected — running " + std::to_string(pending) + " deep-work task(s).")});

    // Heavy model only needed for legacy completion path; tool dispatch and
    // daily_summary can run without it. Still request it so mixed queues work.
    inf_.requestHeavy(true);

    while (idle_) {
        auto next = popNextQueued();
        if (!next) break;
        runTask(*next);
    }

    inf_.requestHeavy(false);
    draining_ = false;
    PM_INFO("TaskScheduler: queue drained / paused (idle={})", idle_.load());
}

std::optional<TaskScheduler::QueuedTask> TaskScheduler::popNextQueued() {
    // Highest priority first, then oldest. Atomically claim by flipping to
    // 'running' so a re-entrant drain can't grab the same row.
    QueuedTask t;
    bool found = false;
    db_.query(
        "SELECT id,type,params_json,priority FROM tasks "
        "WHERE status='queued' ORDER BY priority DESC, created_at ASC, id ASC LIMIT 1",
        {},
        [&](const Row& r) {
            t.id = r.i64(0);
            t.type = r.text(1);
            t.params_json = r.text(2);
            t.priority = static_cast<int>(r.i64(3));
            found = true;
        });
    if (!found) return std::nullopt;

    int64_t claimed = db_.exec(
        "UPDATE tasks SET status='running', updated_at=?2 WHERE id=?1 AND status='queued'",
        {t.id, to_unix(Clock::now())});
    (void)claimed;
    EventBus::instance().publishTask({t.id, QString::fromStdString(t.type), "running", ""});
    return t;
}

void TaskScheduler::runTask(const QueuedTask& qt) {
    PM_INFO("TaskScheduler: running task {} ({})", qt.id, qt.type);

    json params = json::object();
    try {
        if (!qt.params_json.empty()) params = json::parse(qt.params_json);
    } catch (const std::exception& e) {
        PM_WARN("TaskScheduler: bad params_json for task {}: {}", qt.id, e.what());
    }

    auto complete = [&](const json& result, bool ok) {
        const char* status = ok ? "done" : "error";
        db_.exec("UPDATE tasks SET status=?2, result_json=?3, updated_at=?4 WHERE id=?1",
                 {qt.id, std::string(status), result.dump(), to_unix(Clock::now())});
        const QString result_json = QString::fromStdString(result.dump());
        const QString detail = ok
            ? QString()
            : QString::fromStdString(result.value("error", std::string("failed")));
        EventBus::instance().publishTask(
            {qt.id, QString::fromStdString(qt.type), QString::fromUtf8(status), detail});
        emit taskFinished(qt.id, result_json);
        PM_INFO("TaskScheduler: task {} finished status={} type={}", qt.id, status, qt.type);
    };

    // --- 1) Known tool name → ITool::invoke --------------------------------
    if (tools_) {
        if (ITool* tool = tools_->get(qt.type)) {
            ToolContext tctx;
            tctx.inference          = &inf_;
            tctx.db                 = &db_;
            tctx.memory             = memory_;
            tctx.active_user_id     = -1;
            tctx.active_personality = "";
            ToolResult result;
            try {
                result = tool->invoke(params, tctx);
            } catch (const std::exception& e) {
                result.ok = false;
                result.content = {{"error", e.what()}};
                result.summary = std::string("tool threw: ") + e.what();
                PM_ERROR("TaskScheduler: tool '{}' threw: {}", qt.type, e.what());
            }
            json out;
            out["type"] = qt.type;
            out["tool"] = qt.type;
            out["ok"] = result.ok;
            out["content"] = result.content;
            out["summary"] = result.summary;
            out["text"] = result.summary.empty() ? result.content.dump() : result.summary;
            out["completed_at"] = to_unix(Clock::now());
            if (!result.ok)
                out["error"] = result.content.value("error", result.summary);
            complete(out, result.ok);
            return;
        }
    }

    // --- 2) daily_summary → MemoryService::summarizeDay --------------------
    if (qt.type == "daily_summary" && memory_) {
        int64_t day = to_unix(Clock::now());
        if (params.contains("day") && params["day"].is_number_integer())
            day = params["day"].get<int64_t>();
        else if (params.contains("day_unix") && params["day_unix"].is_number_integer())
            day = params["day_unix"].get<int64_t>();

        std::string text;
        bool ok = true;
        try {
            text = memory_->summarizeDay(day);
        } catch (const std::exception& e) {
            ok = false;
            text = e.what();
            PM_ERROR("TaskScheduler: summarizeDay threw: {}", e.what());
        }
        json out;
        out["type"] = qt.type;
        out["text"] = text;
        out["day"] = day;
        out["completed_at"] = to_unix(Clock::now());
        if (!ok) out["error"] = text;
        complete(out, ok);
        return;
    }

    // --- 3) Legacy completion (heavy model) --------------------------------
    ChatRequest req;
    req.model_id   = "";                 // active heavy model is selected by InferenceManager
    req.request_id = "task-" + std::to_string(qt.id);
    req.sampling.max_tokens = 4096;      // deep work wants room
    req.messages.push_back({Role::System, systemPromptFor(qt.type)});
    req.messages.push_back({Role::User,   userPromptFor(qt.type, params)});

    bool ok = false;
    std::string text;
    if (collector_)
        text = collector_->run(inf_, req, /*timeout_ms=*/600'000, &ok);
    else
        ok = false;

    json result;
    result["type"] = qt.type;
    result["text"] = text;
    result["completed_at"] = to_unix(Clock::now());
    if (!ok) result["error"] = "inference timed out or produced no output";

    complete(result, ok);
}

} // namespace polymath
