#include "queue_tool.h"
#include "tool_support.h"
#include "database.h"
#include "event_bus.h"
#include "logging.h"

#include <QString>

// queue_deep_task — enqueue a heavy job into the `tasks` table. The TaskScheduler
// (scheduler module) drains queued tasks by priority when the machine is idle,
// loading the Heavy model as needed. We insert the durable row here and emit a
// TaskEvent so the UI's Task Queue reflects it immediately.

namespace polymath {

std::string QueueDeepTaskTool::name() const { return "queue_deep_task"; }
std::string QueueDeepTaskTool::description() const {
    return "Queue a heavy background task (e.g. research, lab_report, summary) to run when the "
           "machine is idle. Returns a task id; the user is notified when it completes.";
}

nlohmann::json QueueDeepTaskTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"type",     {{"type", "string"},
                          {"description", "Task type: research|lab_report|summary|..."}}},
            {"params",   {{"type", "object"},
                          {"description", "Task-specific parameters (free-form object)"}}},
            {"priority", {{"type", "integer"},
                          {"description", "Higher runs first (default 0)"}}},
        }},
        {"required", {"type"}},
    };
}

ToolResult QueueDeepTaskTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    const std::string type = args.value("type", "");
    if (type.empty())
        return {false, {{"error", "type required"}}, "queue_deep_task: missing type"};

    nlohmann::json params = args.value("params", nlohmann::json::object());
    if (!params.is_object()) params = nlohmann::json::object();
    const int priority = args.value("priority", 0);
    const int64_t now = tool_support::nowUnix();

    const int64_t id = ctx.db->exec(
        "INSERT INTO tasks(type,params_json,priority,status,created_at,updated_at) "
        "VALUES(?1,?2,?3,'queued',?4,?4)",
        {type, params.dump(), priority, now});

    // Let the Task Queue UI reflect the new job right away.
    EventBus::instance().publishTask(
        {id, QString::fromStdString(type), QStringLiteral("queued"), QString()});

    PM_INFO("queue_deep_task: id={} type={} prio={}", id, type, priority);
    nlohmann::json content = {{"task_id", id}, {"type", type}, {"status", "queued"}};
    return {true, std::move(content), "Queued " + type + " task (id " + std::to_string(id) + ")"};
}

} // namespace polymath
