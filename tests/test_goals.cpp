// Goal execution integrity end-to-end (overhaul2 A2 / B-DEADGOAL).
//
// DETERMINISTIC (no model required — always green):
//   1. run_skill actually EXECUTES: invoking the run_skill tool persists a goal
//      AND hands it to the AgentLoop, which drives it to a terminal state (a
//      single shopping_add tool step lands a row). No more silently-parked goals.
//   2. Session rejoin: a goal parked `waiting_agent` on an external session is
//      resumed when a fake AgentSessionEvent{kind:Result} is published on the
//      EventBus — the parked step is marked done with the injected transcript
//      tail and the goal continues to completion.
//
// Model-optional: neither test needs a GGUF (all steps are pre-authored tool
// steps / offline-completable), so the suite is green on a CI box with no models.

#include "agent_runtime.h"
#include "agent_loop.h"
#include "tool_registry.h"
#include "inference_manager.h"
#include "task_scheduler.h"
#include "memory_service.h"
#include "database.h"
#include "config.h"
#include "paths.h"
#include "event_bus.h"
#include "schema.h"
#include "types.h"
#include "i_tool.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QObject>
#include <QThread>

#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace polymath;

namespace {

int64_t scalarCount(Database& db, const std::string& sql,
                    const std::vector<nlohmann::json>& params = {}) {
    int64_t n = 0;
    db.query(sql, params, [&](const Row& r) { n = r.i64(0); });
    return n;
}

std::string goalStatus(Database& db, int64_t gid) {
    std::string s;
    db.query("SELECT status FROM goals WHERE id=?1", {gid},
             [&](const Row& r) { s = r.text(0); });
    return s;
}

// Pump the event loop until goal `gid` reaches a terminal status or `ms` elapse.
bool waitForGoalTerminal(Database& db, int64_t gid, int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        const std::string s = goalStatus(db, gid);
        if (s == "done" || s == "failed" || s == "cancelled") return true;
        QThread::msleep(10);
    }
    return false;
}

void writeFile(const std::filesystem::path& p, const std::string& body) {
    std::filesystem::create_directories(p.parent_path());
    FILE* f = std::fopen(p.string().c_str(), "wb");
    assert(f && "could not open file for writing");
    std::fwrite(body.data(), 1, body.size(), f);
    std::fclose(f);
}

// ---------------------------------------------------------------------------
// 1. run_skill executes (B-DEADGOAL)
// ---------------------------------------------------------------------------
void testRunSkillExecutes(AgentRuntime& agent, Database& db) {
    ITool* runSkill = agent.tools().get("run_skill");
    assert(runSkill && "run_skill tool not registered");

    ToolContext ctx;
    ctx.db = &db;   // run_skill persists the goal; execution uses the loop's own deps

    auto r = runSkill->invoke({{"name", "quick_shop"}}, ctx);
    assert(r.ok && "run_skill invoke failed");
    const int64_t gid = r.content.value("goal_id", int64_t{0});
    assert(gid > 0 && "run_skill did not persist a goal");
    // Goal starts active (confirm=false); execution was requested via the runtime.
    assert(goalStatus(db, gid) == "active");

    const bool terminal = waitForGoalTerminal(db, gid, 8000);
    assert(terminal && "run_skill goal never reached a terminal state (dead goal)");
    assert(goalStatus(db, gid) == "done" && "run_skill goal did not complete");
    assert(scalarCount(db,
              "SELECT COUNT(*) FROM shopping_items WHERE item='milk' AND done=0") == 1
           && "run_skill tool step did not run");
    std::puts("  [ok] run_skill executes a persisted goal to done (B-DEADGOAL)");
}

// ---------------------------------------------------------------------------
// 2. Session rejoin: a fake session-complete event resumes a waiting_agent goal
// ---------------------------------------------------------------------------
void testSessionResume(AgentRuntime& agent, Database& db) {
    AgentLoop* loop = agent.loop();
    assert(loop);

    PlanStepRec s0;   // delegate to an external agent session → parks waiting_agent
    s0.kind = "agent_session";
    s0.description = "delegate to claude";
    s0.args = {{"provider", "claude-code"}, {"cwd", "C:/tmp"}, {"prompt", "do it"}};
    PlanStepRec s1;   // follow-up local tool step that must run AFTER rejoin
    s1.kind = "tool";
    s1.tool = "shopping_add";
    s1.description = "add juice";
    s1.args = {{"item", "juice"}};

    const int64_t gid = loop->createGoal("session goal", "chat",
                                         {{"request_id", "sess-test-req"}}, {s0, s1});
    // Park it: no sessions service is wired, so agent_spawn refuses and the goal
    // parks waiting_agent (with an empty session id) — the legacy-safe fallback.
    loop->executeGoal(gid);
    assert(goalStatus(db, gid) == "waiting_agent" && "agent_session step did not park");
    // The follow-up tool step must NOT have run yet.
    assert(scalarCount(db,
              "SELECT COUNT(*) FROM shopping_items WHERE item='juice'") == 0);

    // Attach a known session id so the fake event can find this parked goal.
    {
        std::string ctxJson = "{}";
        db.query("SELECT context_json FROM goals WHERE id=?1", {gid},
                 [&](const Row& r) { ctxJson = r.text(0); });
        auto cj = nlohmann::json::parse(ctxJson, nullptr, false);
        if (!cj.is_object()) cj = nlohmann::json::object();
        cj["waiting_session_id"] = "sess-test";
        db.exec("UPDATE goals SET context_json=?2 WHERE id=?1",
                {gid, cj.dump()});
    }

    // Fire a fake session completion on the bus (the real path from
    // AgentSessionService::publishBus). AgentRuntime is subscribed and rejoins.
    AgentSessionEvent ev;
    ev.session_id = QStringLiteral("sess-test");
    ev.kind = QStringLiteral("Result");
    ev.text = QStringLiteral("agent finished: created the file and exited cleanly");
    ev.ts = to_unix(Clock::now());
    EventBus::instance().publishAgentSessionEvent(ev);

    const bool terminal = waitForGoalTerminal(db, gid, 8000);
    assert(terminal && "waiting_agent goal never resumed after session-complete event");
    assert(goalStatus(db, gid) == "done" && "goal did not complete after rejoin");
    // The parked step got the injected transcript tail; the follow-up ran.
    assert(scalarCount(db,
              "SELECT COUNT(*) FROM shopping_items WHERE item='juice' AND done=0") == 1
           && "follow-up tool step did not run after rejoin");

    const GoalRec g = loop->loadGoal(gid);
    assert(g.steps.size() == 2);
    assert(g.steps[0].status == "done" && "parked agent_session step not marked done");
    assert(g.steps[0].result.is_object());
    assert(g.steps[0].result.value("event", "") == "Result");
    assert(g.steps[0].result.value("transcript_tail", "").find("agent finished")
               != std::string::npos && "session transcript tail not injected");
    std::puts("  [ok] fake session-complete event resumes a waiting_agent goal");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const auto root = std::filesystem::temp_directory_path() / "polymath_goals";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    // Paths must be set BEFORE the tool registry (and its SkillRegistry) is built.
    Paths::instance().setRoot(root);
    Paths::instance().ensureLayout();

    // A deterministic single-step skill (confirm=false so it runs immediately).
    writeFile(root / "skills" / "quick_shop" / "skill.json", R"({
  "name": "quick_shop",
  "description": "test skill: add milk to the shopping list",
  "triggers": ["quick shop"],
  "params": { "type": "object", "properties": {} },
  "confirm": false,
  "steps": [
    { "kind": "tool", "tool": "shopping_add",
      "description": "add milk", "args": { "item": "milk" } }
  ]
})");

    const auto dbPath = root / "goals.db";
    Database db;
    assert(db.open(dbPath.string()));
    Config(db).seedDefaults();

    // Services in AppController order. AgentRuntime's ctor registers the builtin
    // tools (incl. run_skill over the SkillRegistry seeded from root/skills).
    auto inf   = std::make_unique<InferenceManager>(db);
    auto sched = std::make_unique<TaskScheduler>(db, *inf);
    auto mem   = std::make_unique<MemoryService>(db, *inf);
    // Heap + leak on purpose (like test_harness_e2e): keeps teardown out of scope.
    auto* agent = new AgentRuntime(db, *inf, *sched, mem.get());
    agent->start();   // inline (this thread): wires the bus + join timer + resume

    std::puts("test_goals: A2 goal execution integrity");
    testRunSkillExecutes(*agent, db);
    testSessionResume(*agent, db);

    agent->stop();
    // agent intentionally leaked at process exit.
    db.close();
    std::filesystem::remove_all(root, ec);
    std::puts("test_goals: ALL CHECKS PASSED");
    return 0;
}
