#include "orchestration_tools.h"
#include "tool_support.h"
#include "agent_loop.h"      // AgentLoop::executingGoalId / caps
#include "agent_runtime.h"   // requestGoalExecution
#include "database.h"
#include "logging.h"

#include <string>

// spawn_subtask / subtask_status — D2 goal-tree tools. Parent park/resume lives
// in AgentLoop; these tools only create/inspect child goals.

namespace polymath {

namespace {

bool validJoinPolicy(const std::string& p) {
    return p == "all" || p == "any" || p == "first_success";
}

// Walk parent_id chain; root depth = 0.
int depthOfGoal(Database& db, int64_t goal_id) {
    int depth = 0;
    int64_t cur = goal_id;
    for (int i = 0; i < 16 && cur > 0; ++i) {
        int64_t parent = 0;
        db.query("SELECT parent_id FROM goals WHERE id=?1", {cur},
                 [&](const Row& r) {
                     if (!r.isNull(0)) parent = r.i64(0);
                 });
        if (parent <= 0) break;
        ++depth;
        cur = parent;
    }
    return depth;
}

int countChildren(Database& db, int64_t parent_id) {
    int n = 0;
    db.query("SELECT COUNT(*) FROM goals WHERE parent_id=?1", {parent_id},
             [&](const Row& r) { n = static_cast<int>(r.i64(0)); });
    return n;
}

// Ensure AgentLoop has applied D2 columns when a loop is live; otherwise do a
// best-effort ALTER (same SQL as ensureGoalTreeColumns) so tools work in tests
// without a full runtime.
void ensureColumns(Database& db) {
    bool has_parent = false, has_join = false;
    db.query("PRAGMA table_info(goals)", {}, [&](const Row& r) {
        const std::string name = r.text(1);
        if (name == "parent_id") has_parent = true;
        if (name == "join_policy") has_join = true;
    });
    if (!has_parent)
        db.exec("ALTER TABLE goals ADD COLUMN parent_id INTEGER");
    if (!has_join)
        db.exec("ALTER TABLE goals ADD COLUMN join_policy TEXT NOT NULL DEFAULT 'all'");
    db.exec("CREATE INDEX IF NOT EXISTS idx_goals_parent_id ON goals(parent_id)");
}

} // namespace

// ---------------------------------------------------------------------------
//  spawn_subtask
// ---------------------------------------------------------------------------

std::string SpawnSubtaskTool::name() const { return "spawn_subtask"; }

std::string SpawnSubtaskTool::description() const {
    return "Spawn a child subtask under the current (or given) parent goal. "
           "Creates a child goals row with parent_id, inserts plan steps "
           "(prompt, skill, or agent_session), and queues execution. The parent "
           "parks as waiting_children when its own steps finish; join_policy "
           "(all|any|first_success) controls when it resumes with a results "
           "digest. Depth cap 2, max 8 children per parent.";
}

nlohmann::json SpawnSubtaskTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"title", {{"type", "string"},
                       {"description", "Short label for the child goal"}}},
            {"prompt", {{"type", "string"},
                        {"description", "Free-form instruction (kind=prompt step)"}}},
            {"skill", {{"type", "string"},
                       {"description", "Registered skill name (kind=skill step)"}}},
            {"task", {{"type", "object"},
                      {"description", "Alternative: {skill?} or {prompt?}"},
                      {"properties", {
                          {"skill",  {{"type", "string"}}},
                          {"prompt", {{"type", "string"}}},
                          {"params", {{"type", "object"}}},
                      }}}},
            {"kind", {{"type", "string"},
                      {"description",
                       "prompt | skill | agent_session (default: inferred from args)"}}},
            {"parent_id", {{"type", "integer"},
                           {"description",
                            "Parent goal id (default: goal currently executing)"}}},
            {"join_policy", {{"type", "string"},
                             {"description",
                              "Set on parent: all | any | first_success (default all)"}}},
            {"provider", {{"type", "string"},
                          {"description", "For kind=agent_session: external provider id"}}},
            {"args", {{"type", "object"},
                      {"description", "Extra step args (skill params / session args)"}}},
        }},
        {"required", {"title"}},
    };
}

ToolResult SpawnSubtaskTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    if (!ctx.db)
        return {false, {{"error", "no database"}}, "spawn_subtask: no db"};

    ensureColumns(*ctx.db);

    const std::string title = args.value("title", "");
    if (title.empty())
        return {false, {{"error", "title required"}}, "spawn_subtask: missing title"};

    // Resolve task payload: top-level prompt/skill or nested task{}
    const nlohmann::json task = args.value("task", nlohmann::json::object());
    std::string prompt = args.value("prompt", "");
    std::string skill  = args.value("skill", "");
    nlohmann::json step_args = args.value("args", nlohmann::json::object());
    if (!step_args.is_object()) step_args = nlohmann::json::object();
    if (task.is_object()) {
        if (prompt.empty()) prompt = task.value("prompt", "");
        if (skill.empty())  skill  = task.value("skill", "");
        if (task.contains("params") && task["params"].is_object()) {
            for (auto it = task["params"].begin(); it != task["params"].end(); ++it)
                if (!step_args.contains(it.key())) step_args[it.key()] = it.value();
        }
    }

    std::string kind = args.value("kind", "");
    if (kind.empty()) {
        if (!skill.empty()) kind = "skill";
        else if (args.contains("provider") ||
                 (step_args.contains("provider") && !step_args.value("provider", "").empty()))
            kind = "agent_session";
        else kind = "prompt";
    }
    if (kind != "prompt" && kind != "skill" && kind != "agent_session") {
        return {false, {{"error", "kind must be prompt|skill|agent_session"}},
                "spawn_subtask: bad kind"};
    }
    if (kind == "prompt" && prompt.empty())
        prompt = title;   // usable default
    if (kind == "skill" && skill.empty())
        return {false, {{"error", "skill name required for kind=skill"}},
                "spawn_subtask: missing skill"};
    if (kind == "agent_session") {
        if (args.contains("provider") && !args.value("provider", "").empty())
            step_args["provider"] = args.value("provider", "");
        if (!step_args.contains("provider") || step_args.value("provider", "").empty())
            return {false, {{"error", "provider required for kind=agent_session"}},
                    "spawn_subtask: missing provider"};
    }

    int64_t parent_id = args.value("parent_id", int64_t{0});
    if (parent_id <= 0) parent_id = AgentLoop::executingGoalId();
    if (parent_id <= 0) {
        return {false,
                {{"error", "parent_id required (no goal currently executing)"}},
                "spawn_subtask: no parent"};
    }

    // Parent must exist and not be terminal.
    std::string parent_status;
    ctx.db->query("SELECT status FROM goals WHERE id=?1", {parent_id},
                  [&](const Row& r) { parent_status = r.text(0); });
    if (parent_status.empty())
        return {false, {{"error", "parent goal not found"}, {"parent_id", parent_id}},
                "spawn_subtask: parent missing"};
    if (parent_status == "done" || parent_status == "failed" ||
        parent_status == "cancelled") {
        return {false,
                {{"error", "parent goal is terminal"}, {"status", parent_status}},
                "spawn_subtask: parent terminal"};
    }

    // Depth cap: child depth = parent depth + 1 must be <= kGoalTreeDepthMax.
    const int child_depth = depthOfGoal(*ctx.db, parent_id) + 1;
    if (child_depth > AgentLoop::kGoalTreeDepthMax) {
        return {false,
                {{"error", "goal tree depth cap exceeded"},
                 {"depth", child_depth},
                 {"max", AgentLoop::kGoalTreeDepthMax}},
                "spawn_subtask: depth cap (" +
                    std::to_string(AgentLoop::kGoalTreeDepthMax) + ")"};
    }

    // Child cap per parent.
    const int existing = countChildren(*ctx.db, parent_id);
    if (existing >= AgentLoop::kGoalTreeChildCap) {
        return {false,
                {{"error", "child cap exceeded"},
                 {"children", existing},
                 {"max", AgentLoop::kGoalTreeChildCap}},
                "spawn_subtask: child cap (" +
                    std::to_string(AgentLoop::kGoalTreeChildCap) + ")"};
    }

    // Optional join_policy update on the parent.
    std::string join_policy = args.value("join_policy", "");
    if (!join_policy.empty()) {
        if (!validJoinPolicy(join_policy)) {
            return {false, {{"error", "join_policy must be all|any|first_success"}},
                    "spawn_subtask: bad join_policy"};
        }
        ctx.db->exec("UPDATE goals SET join_policy=?1, updated_at=?2 WHERE id=?3",
                     {join_policy, tool_support::nowUnix(), parent_id});
    } else {
        // Read current for response.
        ctx.db->query("SELECT join_policy FROM goals WHERE id=?1", {parent_id},
                      [&](const Row& r) {
                          if (!r.isNull(0) && !r.text(0).empty())
                              join_policy = r.text(0);
                      });
        if (join_policy.empty()) join_policy = "all";
    }

    // Build the single plan step for the child.
    std::string step_desc;
    std::string step_tool;
    nlohmann::json step_json = nlohmann::json::object();
    if (kind == "prompt") {
        step_desc = prompt;
    } else if (kind == "skill") {
        step_desc = "Run skill " + skill;
        step_tool = skill;
        step_json = step_args;
        if (!step_json.contains("name")) step_json["name"] = skill;
    } else { // agent_session
        step_desc = "External agent session";
        step_tool = "agent_spawn";
        step_json = step_args;
        if (!prompt.empty() && !step_json.contains("prompt"))
            step_json["prompt"] = prompt;
    }

    nlohmann::json ctx_json = {
        {"parent_id", parent_id},
        {"trace", nlohmann::json::array()},
        {"spawned_by", "spawn_subtask"},
    };
    if (!prompt.empty()) ctx_json["user_text"] = prompt;

    const int64_t now = tool_support::nowUnix();
    const int64_t child_id = ctx.db->exec(
        "INSERT INTO goals(title,status,origin,context_json,created_at,updated_at,"
        "parent_id,join_policy) VALUES(?1,'active','agent',?2,?3,?3,?4,'all')",
        {title, ctx_json.dump(), now, parent_id});
    if (child_id < 0)
        return {false, {{"error", "insert failed"}}, "spawn_subtask: db insert failed"};

    ctx.db->exec(
        "INSERT INTO plan_steps(goal_id,idx,description,kind,tool,args_json,"
        "status,attempts,updated_at) VALUES(?1,0,?2,?3,?4,?5,'pending',0,?6)",
        {child_id, step_desc, kind,
         step_tool.empty() ? nlohmann::json(nullptr) : nlohmann::json(step_tool),
         step_json.dump(), now});

    // Pure-local children queue serially via the agent worker; agent_session
    // children can run in parallel once parked waiting_agent (external).
    requestGoalExecution(child_id);

    PM_INFO("spawn_subtask: child={} parent={} kind={} title='{}' policy={}",
            child_id, parent_id, kind, title, join_policy);

    nlohmann::json content = {
        {"child_id", child_id},
        {"parent_id", parent_id},
        {"title", title},
        {"kind", kind},
        {"join_policy", join_policy},
        {"depth", child_depth},
        {"children_of_parent", existing + 1},
    };
    return {true, std::move(content),
            "Spawned subtask \"" + title + "\" [goal " + std::to_string(child_id) +
                "] under parent " + std::to_string(parent_id)};
}

// ---------------------------------------------------------------------------
//  subtask_status
// ---------------------------------------------------------------------------

std::string SubtaskStatusTool::name() const { return "subtask_status"; }

std::string SubtaskStatusTool::description() const {
    return "List child subtasks of a parent goal (status, title, result summary). "
           "parent_id defaults to the goal currently executing.";
}

nlohmann::json SubtaskStatusTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"parent_id", {{"type", "integer"},
                           {"description",
                            "Parent goal id (default: goal currently executing)"}}},
        }},
    };
}

ToolResult SubtaskStatusTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    if (!ctx.db)
        return {false, {{"error", "no database"}}, "subtask_status: no db"};

    ensureColumns(*ctx.db);

    int64_t parent_id = args.value("parent_id", int64_t{0});
    if (parent_id <= 0) parent_id = AgentLoop::executingGoalId();
    if (parent_id <= 0) {
        return {false,
                {{"error", "parent_id required (no goal currently executing)"}},
                "subtask_status: no parent"};
    }

    std::string join_policy = "all";
    std::string parent_status;
    ctx.db->query("SELECT status,join_policy FROM goals WHERE id=?1", {parent_id},
                  [&](const Row& r) {
                      parent_status = r.text(0);
                      if (!r.isNull(1) && !r.text(1).empty())
                          join_policy = r.text(1);
                  });
    if (parent_status.empty())
        return {false, {{"error", "parent goal not found"}, {"parent_id", parent_id}},
                "subtask_status: parent missing"};

    nlohmann::json children = nlohmann::json::array();
    int done = 0, failed = 0, active = 0;
    ctx.db->query(
        "SELECT id,title,status,result_json FROM goals WHERE parent_id=?1 "
        "ORDER BY id ASC",
        {parent_id},
        [&](const Row& r) {
            nlohmann::json c;
            c["id"] = r.i64(0);
            c["title"] = r.text(1);
            c["status"] = r.text(2);
            const std::string st = r.text(2);
            if (st == "done") ++done;
            else if (st == "failed" || st == "cancelled") ++failed;
            else ++active;
            std::string summary;
            if (!r.isNull(3)) {
                auto res = nlohmann::json::parse(r.text(3), nullptr, false);
                if (res.is_object()) summary = res.value("summary", "");
            }
            c["summary"] = summary;
            children.push_back(std::move(c));
        });

    nlohmann::json content = {
        {"parent_id", parent_id},
        {"parent_status", parent_status},
        {"join_policy", join_policy},
        {"children", std::move(children)},
        {"done", done},
        {"failed", failed},
        {"active", active},
        {"total", done + failed + active},
    };
    const std::string summary =
        "Parent " + std::to_string(parent_id) + " (" + parent_status + "): " +
        std::to_string(done) + " done / " + std::to_string(failed) +
        " failed / " + std::to_string(active) + " active";
    return {true, std::move(content), summary};
}

} // namespace polymath
