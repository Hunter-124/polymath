#include "schedule_tools.h"
#include "tool_support.h"
#include "scheduler_util.h"
#include "database.h"
#include "logging.h"

#include <ctime>

// schedule_task / list_schedules / cancel_schedule — persist/inspect/cancel
// `scheduled_goals` rows. The ProactiveEngine (scheduler module) owns firing;
// see src/scheduler/proactive_engine.cpp.

namespace polymath {

namespace {

// Resolve "HH:MM" to the next matching unix timestamp (today, else tomorrow).
// Mirrors reminders.cpp's resolveClockTime (kept local: different TU, tiny).
int64_t resolveClockTime(const std::string& hhmm) {
    auto colon = hhmm.find(':');
    if (colon == std::string::npos) return 0;
    int h = 0, m = 0;
    try {
        h = std::stoi(hhmm.substr(0, colon));
        m = std::stoi(hhmm.substr(colon + 1));
    } catch (...) { return 0; }
    if (h < 0 || h > 23 || m < 0 || m > 59) return 0;

    const std::time_t now = static_cast<std::time_t>(tool_support::nowUnix());
    std::tm lt{};
#if defined(_WIN32)
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    lt.tm_hour = h;
    lt.tm_min  = m;
    lt.tm_sec  = 0;
    std::time_t due = std::mktime(&lt);
    if (due <= now) due += 24 * 3600;   // already passed today -> tomorrow
    return static_cast<int64_t>(due);
}

} // namespace

// ---------------------------------------------------------------------------
//  schedule_task
// ---------------------------------------------------------------------------

std::string ScheduleTaskTool::name() const { return "schedule_task"; }

std::string ScheduleTaskTool::description() const {
    return "Schedule a timed or recurring agent task. Fires a real agent goal "
           "(a skill or a free-form prompt, run through the full plan/execute "
           "harness) at the scheduled time(s). when: exactly one of "
           "at (unix seconds or \"HH:MM\"), every_s (interval seconds), or "
           "rrule (e.g. \"FREQ=DAILY\"); optional when.anchor (\"HH:MM\") pins "
           "the first every_s/rrule occurrence. task: exactly one of skill or "
           "prompt. deliver: chat|voice|notify (default chat).";
}

nlohmann::json ScheduleTaskTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"title", {{"type", "string"},
                       {"description", "Short label shown in Tasks ▸ Scheduled"}}},
            {"when", {{"type", "object"},
                      {"description", "Exactly one of at/every_s/rrule"},
                      {"properties", {
                          {"at",      {{"type", "string"},
                                       {"description", "Unix seconds, or \"HH:MM\" (today, else tomorrow)"}}},
                          {"every_s", {{"type", "integer"},
                                       {"description", "Recurring interval in seconds"}}},
                          {"rrule",   {{"type", "string"},
                                       {"description", "iCal RRULE, e.g. FREQ=DAILY;INTERVAL=1"}}},
                          {"anchor",  {{"type", "string"},
                                       {"description", "Optional \"HH:MM\" local time for the first occurrence"}}},
                      }}}},
            {"task", {{"type", "object"},
                      {"description", "Exactly one of skill/prompt"},
                      {"properties", {
                          {"skill",  {{"type", "string"}, {"description", "Registered skill name"}}},
                          {"prompt", {{"type", "string"}, {"description", "Free-form instruction"}}},
                          {"params", {{"type", "object"}, {"description", "Params for the skill (ignored for prompt)"}}},
                      }}}},
            {"deliver", {{"type", "string"},
                         {"description", "chat|voice|notify (default chat)"}}},
        }},
        {"required", {"title", "when", "task"}},
    };
}

ToolResult ScheduleTaskTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    const std::string title = args.value("title", "");
    if (title.empty())
        return {false, {{"error", "title required"}}, "schedule_task: missing title"};

    const nlohmann::json when = args.value("when", nlohmann::json::object());
    const nlohmann::json task = args.value("task", nlohmann::json::object());
    if (!when.is_object() || !task.is_object())
        return {false, {{"error", "when/task must be objects"}}, "schedule_task: bad args"};

    const bool has_at    = when.contains("at")    && !when["at"].is_null();
    const bool has_every = when.contains("every_s") && !when["every_s"].is_null();
    const bool has_rrule = when.contains("rrule") && !when["rrule"].is_null() &&
                            !when.value("rrule", std::string{}).empty();
    if (static_cast<int>(has_at) + static_cast<int>(has_every) + static_cast<int>(has_rrule) != 1) {
        return {false, {{"error", "when needs exactly one of at/every_s/rrule"}},
                "schedule_task: bad when"};
    }

    const bool has_skill  = task.contains("skill")  && !task.value("skill", std::string{}).empty();
    const bool has_prompt = task.contains("prompt") && !task.value("prompt", std::string{}).empty();
    if (static_cast<int>(has_skill) + static_cast<int>(has_prompt) != 1) {
        return {false, {{"error", "task needs exactly one of skill/prompt"}},
                "schedule_task: bad task"};
    }

    std::string deliver = args.value("deliver", "chat");
    if (deliver != "chat" && deliver != "voice" && deliver != "notify") {
        return {false, {{"error", "deliver must be chat|voice|notify"}},
                "schedule_task: bad deliver"};
    }

    const std::string anchor = when.value("anchor", "");
    std::string kind, spec;
    int64_t next_fire = 0;

    if (has_at) {
        kind = "at";
        const nlohmann::json& at = when["at"];
        if (at.is_number()) {
            next_fire = at.get<int64_t>();
        } else if (at.is_string()) {
            const std::string s = at.get<std::string>();
            next_fire = resolveClockTime(s);
            if (next_fire == 0) {
                try { next_fire = std::stoll(s); } catch (...) { next_fire = 0; }
            }
        }
        if (next_fire <= 0)
            return {false, {{"error", "could not resolve when.at"}}, "schedule_task: bad at"};
        spec = std::to_string(next_fire);
    } else if (has_every) {
        kind = "every";
        const int64_t every_s = when["every_s"].get<int64_t>();
        if (every_s <= 0)
            return {false, {{"error", "every_s must be positive"}}, "schedule_task: bad every_s"};
        spec = std::to_string(every_s);
        next_fire = !anchor.empty() ? resolveClockTime(anchor)
                                    : sched_util::advanceEvery(tool_support::nowUnix(), every_s);
        if (next_fire <= 0)
            return {false, {{"error", "could not resolve first occurrence"}}, "schedule_task: bad when"};
    } else {
        kind = "rrule";
        spec = when.value("rrule", "");
        next_fire = !anchor.empty() ? resolveClockTime(anchor)
                                    : sched_util::advanceRrule(spec, tool_support::nowUnix());
        if (next_fire <= 0)
            return {false, {{"error", "unrecognised or unadvanceable rrule"}},
                    "schedule_task: bad rrule"};
    }

    const std::string skill  = has_skill  ? task.value("skill", "")  : std::string{};
    const std::string prompt = has_prompt ? task.value("prompt", "") : std::string{};
    nlohmann::json params = task.value("params", nlohmann::json::object());
    if (!params.is_object()) params = nlohmann::json::object();

    const int64_t id = ctx.db->exec(
        "INSERT INTO scheduled_goals(title,prompt,skill,params_json,kind,spec,next_fire,"
        "last_fire,enabled,deliver,source,created_at) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7,NULL,1,?8,'user',?9)",
        {title, prompt, skill, params.dump(), kind, spec, next_fire, deliver,
         tool_support::nowUnix()});
    if (id < 0)
        return {false, {{"error", "insert failed"}}, "schedule_task: db insert failed"};

    PM_INFO("schedule_task: id={} title='{}' kind={} spec='{}' next_fire={} deliver={}",
            id, title, kind, spec, next_fire, deliver);

    nlohmann::json content = {
        {"schedule_id", id}, {"title", title}, {"kind", kind}, {"spec", spec},
        {"next_fire", next_fire}, {"deliver", deliver},
    };
    return {true, std::move(content),
            "Scheduled \"" + title + "\" (" + kind + ", next_fire=" +
                std::to_string(next_fire) + ") [id " + std::to_string(id) + "]"};
}

// ---------------------------------------------------------------------------
//  list_schedules
// ---------------------------------------------------------------------------

std::string ListSchedulesTool::name() const { return "list_schedules"; }

std::string ListSchedulesTool::description() const {
    return "List scheduled agent goals (timed/recurring), with next-fire time, "
           "kind, and enabled state.";
}

nlohmann::json ListSchedulesTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"include_disabled", {{"type", "boolean"},
                                  {"description", "Include disabled/one-shot-fired rows (default false)"}}},
        }},
    };
}

ToolResult ListSchedulesTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    const bool includeDisabled = args.value("include_disabled", false);

    std::string sql =
        "SELECT id,title,kind,spec,next_fire,last_fire,enabled,deliver,skill,prompt "
        "FROM scheduled_goals";
    if (!includeDisabled) sql += " WHERE enabled=1";
    sql += " ORDER BY (next_fire IS NULL), next_fire ASC";

    nlohmann::json rows = nlohmann::json::array();
    ctx.db->query(sql, {}, [&](const Row& r) {
        rows.push_back({
            {"id",         r.i64(0)},
            {"title",      r.text(1)},
            {"kind",       r.text(2)},
            {"spec",       r.text(3)},
            {"next_fire",  r.isNull(4) ? nlohmann::json(nullptr) : nlohmann::json(r.i64(4))},
            {"last_fire",  r.isNull(5) ? nlohmann::json(nullptr) : nlohmann::json(r.i64(5))},
            {"enabled",    r.i64(6) != 0},
            {"deliver",    r.text(7)},
            {"skill",      r.text(8)},
            {"prompt",     r.text(9)},
        });
    });

    const std::string summary = std::to_string(rows.size()) + " scheduled goal(s)";
    return {true, {{"schedules", rows}}, summary};
}

// ---------------------------------------------------------------------------
//  cancel_schedule
// ---------------------------------------------------------------------------

std::string CancelScheduleTool::name() const { return "cancel_schedule"; }

std::string CancelScheduleTool::description() const {
    return "Cancel a scheduled agent goal by id (disables it; stops future firing).";
}

nlohmann::json CancelScheduleTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"id", {{"type", "integer"}, {"description", "scheduled_goals row id"}}},
        }},
        {"required", {"id"}},
    };
}

ToolResult CancelScheduleTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    if (!args.contains("id") || !args["id"].is_number())
        return {false, {{"error", "id required"}}, "cancel_schedule: missing id"};
    const int64_t id = args["id"].get<int64_t>();

    bool existed = false;
    ctx.db->query("SELECT 1 FROM scheduled_goals WHERE id=?1", {id},
                  [&](const Row&) { existed = true; });
    if (!existed)
        return {false, {{"error", "not found"}}, "cancel_schedule: no such id " + std::to_string(id)};

    ctx.db->exec("UPDATE scheduled_goals SET enabled=0 WHERE id=?1", {id});
    PM_INFO("cancel_schedule: id={} disabled", id);

    nlohmann::json content = {{"id", id}, {"enabled", false}};
    return {true, std::move(content), "Cancelled schedule " + std::to_string(id)};
}

} // namespace polymath
