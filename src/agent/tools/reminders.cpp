#include "reminders.h"
#include "tool_support.h"
#include "database.h"
#include "logging.h"

#include <chrono>
#include <ctime>

// set_reminder — insert a row into `reminders`. The ProactiveEngine (scheduler
// module) polls this table and emits ReminderFired on the EventBus when due, so
// this tool only persists; it never fires directly.
//
// Time can be given as:
//   due_at:      unix seconds (absolute), or
//   in_minutes:  relative offset from now, or
//   at:          "HH:MM" today (or tomorrow if already past).
// Optional rrule (recurrence) and condition (e.g. "someone_home") are stored
// verbatim for the engine to interpret.

namespace polymath {

namespace {

// Resolve "HH:MM" to the next matching unix timestamp (today, else tomorrow).
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

std::string SetReminderTool::name() const { return "set_reminder"; }
std::string SetReminderTool::description() const {
    return "Set a reminder. Specify when via in_minutes, at (\"HH:MM\"), or due_at (unix seconds); "
           "optionally a recurrence rrule or a condition like \"someone_home\".";
}

nlohmann::json SetReminderTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"text",       {{"type", "string"},  {"description", "What to be reminded about"}}},
            {"in_minutes", {{"type", "integer"}, {"description", "Fire this many minutes from now"}}},
            {"at",         {{"type", "string"},  {"description", "Clock time today/tomorrow, \"HH:MM\""}}},
            {"due_at",     {{"type", "integer"}, {"description", "Absolute unix timestamp (seconds)"}}},
            {"rrule",      {{"type", "string"},  {"description", "Optional iCal RRULE for recurrence"}}},
            {"condition",  {{"type", "string"},  {"description", "Optional condition (e.g. someone_home)"}}},
        }},
        {"required", {"text"}},
    };
}

ToolResult SetReminderTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    const std::string text = args.value("text", "");
    if (text.empty())
        return {false, {{"error", "text required"}}, "set_reminder: missing text"};

    const std::string rrule     = args.value("rrule", "");
    const std::string condition = args.value("condition", "");

    // Resolve the due time from whichever field is present.
    int64_t due_at = 0;
    if (args.contains("due_at") && args["due_at"].is_number())
        due_at = args["due_at"].get<int64_t>();
    else if (args.contains("in_minutes") && args["in_minutes"].is_number())
        due_at = tool_support::nowUnix() + args["in_minutes"].get<int64_t>() * 60;
    else if (args.value("at", "") != "")
        due_at = resolveClockTime(args.value("at", std::string{}));

    if (due_at == 0 && condition.empty()) {
        return {false, {{"error", "no due time or condition given"}},
                "set_reminder: needs in_minutes/at/due_at or a condition"};
    }

    // due_at NULL means a condition-based reminder (schema allows null due_at).
    nlohmann::json dueParam = (due_at == 0) ? nlohmann::json(nullptr) : nlohmann::json(due_at);
    const int64_t id = ctx.db->exec(
        "INSERT INTO reminders(text,due_at,rrule,condition,fired,created_at) "
        "VALUES(?1,?2,?3,?4,0,?5)",
        {text, dueParam, rrule, condition, tool_support::nowUnix()});

    PM_INFO("set_reminder: id={} due_at={} cond='{}'", id, due_at, condition);
    nlohmann::json content = {
        {"reminder_id", id},
        {"text", text},
        {"due_at", due_at},
        {"rrule", rrule},
        {"condition", condition},
    };
    std::string when = due_at ? ("at unix " + std::to_string(due_at))
                              : ("when " + condition);
    return {true, std::move(content), "Reminder set: \"" + text + "\" " + when};
}

} // namespace polymath
