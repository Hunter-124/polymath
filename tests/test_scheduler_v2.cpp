// Scheduler v2 (overhaul2 D1 — timed/recurring agent goals).
//
// DETERMINISTIC (no model required — always green):
//   1. sched_util::advanceRrule — MINUTELY/HOURLY/DAILY/WEEKLY/MONTHLY step math,
//      including a DST boundary (spring-forward + fall-back) proving the DAILY
//      advance is LOCAL-calendar-based (keeps the local wall-clock hour), not a
//      naive +86400-seconds add (which would drift an hour across the transition).
//   2. sched_util::advanceEvery — plain elapsed-seconds interval math.
//   3. A due `scheduled_goals` row (kind="skill") fires through ProactiveEngine::tick()
//      -> a real `goals` row (origin="schedule") -> AgentRuntime::requestGoalExecution()
//      -> the goal runs to a terminal state (deterministic tool step, same shape as
//      test_goals.cpp's run_skill check). "at" disables the row after firing.
//   4. A due row (kind="prompt") produces the expected goal + plan_step shape
//      (structural only — an unconstrained prompt step needs a model to *complete*,
//      but wiring/creation is asserted without a model).
//   5. kind="every" reschedules next_fire forward (still enabled) after firing.
//   6. Quiet-hours gating: deliver="chat" schedules are held during quiet hours;
//      deliver="notify" schedules bypass quiet hours (accept criterion wording).
//
// Model-optional: nothing here needs a GGUF (all steps are pre-authored tool steps
// or structural-only checks), so the suite is green on a CI box with no models.

#include "scheduler_util.h"
#include "proactive_engine.h"
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
#include "types.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMetaObject>
#include <QThread>

#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace polymath;

namespace {

// --- TZ control (DST test) --------------------------------------------------
// Force a US-Eastern-like DST rule (EST5EDT, 2nd-Sunday-March -> 1st-Sunday-
// November — the modern US rule) so the DST assertions are deterministic
// regardless of the host machine's configured timezone.
void setTz(const char* tz) {
#if defined(_WIN32)
    _putenv_s("TZ", tz);
    _tzset();
#else
    setenv("TZ", tz, 1);
    tzset();
#endif
}

int64_t mkLocal(int year, int mon /*1-12*/, int day, int hour, int min) {
    std::tm lt{};
    lt.tm_year = year - 1900;
    lt.tm_mon  = mon - 1;
    lt.tm_mday = day;
    lt.tm_hour = hour;
    lt.tm_min  = min;
    lt.tm_sec  = 0;
    lt.tm_isdst = -1;
    return static_cast<int64_t>(std::mktime(&lt));
}

std::tm breakLocal(int64_t unix_ts) {
    std::time_t tt = static_cast<std::time_t>(unix_ts);
    std::tm lt{};
#if defined(_WIN32)
    localtime_s(&lt, &tt);
#else
    localtime_r(&tt, &lt);
#endif
    return lt;
}

// --- DB helpers (mirrors test_goals.cpp) ------------------------------------

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

int64_t insertSchedule(Database& db, const std::string& title,
                       const std::string& prompt, const std::string& skill,
                       const std::string& params_json, const std::string& kind,
                       const std::string& spec, int64_t next_fire,
                       const std::string& deliver) {
    return db.exec(
        "INSERT INTO scheduled_goals(title,prompt,skill,params_json,kind,spec,next_fire,"
        "last_fire,enabled,deliver,source,created_at) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7,NULL,1,?8,'test',?9)",
        {title, prompt, skill, params_json, kind, spec, next_fire, deliver,
         static_cast<int64_t>(to_unix(Clock::now()))});
}

void setQuietAlways(Database& db, bool always_quiet) {
    Config cfg(db);
    if (always_quiet) {
        cfg.set(keys::QuietHoursStart, "00:00");
        cfg.set(keys::QuietHoursEnd, "23:59");
    } else {
        // Equal bounds => sched_util::inWindow treats it as an empty window
        // (always false) — deterministic "never quiet" regardless of the
        // real wall-clock time the test happens to run at.
        cfg.set(keys::QuietHoursStart, "00:00");
        cfg.set(keys::QuietHoursEnd, "00:00");
    }
}

void writeFile(const std::filesystem::path& p, const std::string& body) {
    std::filesystem::create_directories(p.parent_path());
    FILE* f = std::fopen(p.string().c_str(), "wb");
    assert(f && "could not open file for writing");
    std::fwrite(body.data(), 1, body.size(), f);
    std::fclose(f);
}

void tickNow(ProactiveEngine& engine) {
    QMetaObject::invokeMethod(&engine, "tick");
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

// ---------------------------------------------------------------------------
// 1. sched_util::advanceRrule — plain (non-DST) step math
// ---------------------------------------------------------------------------
void testAdvanceRruleBasic() {
    setTz("UTC0");
    const int64_t base = mkLocal(2026, 6, 15, 9, 0);   // mid-year, no DST edge nearby

    assert(sched_util::advanceRrule("FREQ=MINUTELY", base) == base + 60);
    assert(sched_util::advanceRrule("FREQ=MINUTELY;INTERVAL=5", base) == base + 5 * 60);
    assert(sched_util::advanceRrule("FREQ=HOURLY", base) == base + 3600);
    assert(sched_util::advanceRrule("FREQ=HOURLY;INTERVAL=3", base) == base + 3 * 3600);
    assert(sched_util::advanceRrule("FREQ=DAILY", base) == base + 86'400);
    assert(sched_util::advanceRrule("FREQ=WEEKLY", base) == base + 7 * 86'400);

    // MONTHLY: calendar month, not a fixed 30-day approximation.
    const int64_t monthly = sched_util::advanceRrule("FREQ=MONTHLY", base);
    std::tm mt = breakLocal(monthly);
    assert(mt.tm_mon == 6 /* July, 0-based */ && mt.tm_mday == 15);

    // Unknown/empty FREQ -> 0 (caller treats as one-shot).
    assert(sched_util::advanceRrule("", base) == 0);
    assert(sched_util::advanceRrule("FREQ=SECONDLY", base) == 0);

    std::puts("  [ok] advanceRrule: MINUTELY/HOURLY/DAILY/WEEKLY/MONTHLY step math");
}

// ---------------------------------------------------------------------------
// 2. sched_util::advanceEvery
// ---------------------------------------------------------------------------
void testAdvanceEvery() {
    assert(sched_util::advanceEvery(1000, 60) == 1060);
    assert(sched_util::advanceEvery(1000, 3600) == 4600);
    assert(sched_util::advanceEvery(1000, 0) == 0);     // invalid interval
    assert(sched_util::advanceEvery(1000, -5) == 0);
    std::puts("  [ok] advanceEvery: literal elapsed-seconds interval");
}

// ---------------------------------------------------------------------------
// 3. DST boundary — DAILY advance keeps the local wall-clock hour
// ---------------------------------------------------------------------------
void testDstBoundary() {
    // Modern US rule: 2nd Sunday of March (spring forward 02:00->03:00),
    // 1st Sunday of November (fall back 02:00->01:00).
    setTz("EST5EDT,M3.2.0,M11.1.0");

    // Sanity-check the extended TZ rule actually took effect on this CRT (a
    // minimal/older CRT could ignore the M3.2.0 DST-transition suffix and fall
    // back to a fixed offset with no DST at all). If so, skip gracefully
    // rather than asserting on an environment limitation unrelated to our code
    // — the DST-safety fix itself (addLocalCalendar in scheduler_util.cpp) is
    // still exercised (as a no-op-DST daily advance) by testAdvanceRruleBasic.
    const std::tm july = breakLocal(mkLocal(2026, 7, 1, 12, 0));
    const std::tm jan  = breakLocal(mkLocal(2026, 1, 1, 12, 0));
    if (!(july.tm_isdst > 0 && jan.tm_isdst == 0)) {
        std::puts("  [skip] DST boundary test: this CRT does not honor extended TZ DST rules");
        setTz("UTC0");
        return;
    }

    // --- Spring forward: 2026-03-08 02:00 local doesn't exist; 2026-03-07 is
    // the Saturday before the 2nd Sunday of March 2026, so 08:00 that day is
    // safely before the transition.
    {
        const int64_t before = mkLocal(2026, 3, 7, 8, 0);   // Sat, still EST (-5)
        const int64_t next   = sched_util::advanceRrule("FREQ=DAILY", before);
        assert(next != 0);
        std::tm nt = breakLocal(next);
        // Local wall-clock time is preserved...
        assert(nt.tm_hour == 8 && nt.tm_min == 0);
        assert(nt.tm_mon == 2 /*March*/ && nt.tm_mday == 8);
        // ...even though only 23 real hours elapsed (a naive +86400s add would
        // land on 09:00 local, one hour later, because EDT is UTC-4 not -5).
        assert(next - before == 23 * 3600);
    }

    // --- Fall back: 1st Sunday of November 2026 is Nov 1; clocks 02:00->01:00.
    // Oct 31 20:00 (8pm) is safely before the transition.
    {
        const int64_t before = mkLocal(2026, 10, 31, 20, 0);   // Sat, EDT (-4)
        const int64_t next   = sched_util::advanceRrule("FREQ=DAILY", before);
        assert(next != 0);
        std::tm nt = breakLocal(next);
        assert(nt.tm_hour == 20 && nt.tm_min == 0);
        assert(nt.tm_mon == 10 /*November*/ && nt.tm_mday == 1);
        // 25 real hours elapsed (an extra hour vs. a plain day) because EST is
        // UTC-5, one hour behind the EDT the starting timestamp was in.
        assert(next - before == 25 * 3600);
    }

    setTz("UTC0");   // restore for subsequent tests
    std::puts("  [ok] advanceRrule DAILY is DST-safe across spring-forward + fall-back");
}

// ---------------------------------------------------------------------------
// 4. A due scheduled_goals row (kind=skill) fires a real goal through A2's
//    execution path and runs it to a terminal state. "at" disables after firing.
// ---------------------------------------------------------------------------
void testSkillScheduleFiresAndDisables(ProactiveEngine& proactive, Database& db) {
    setQuietAlways(db, false);
    const int64_t before_goals = scalarCount(db, "SELECT COUNT(*) FROM goals");

    const int64_t sid = insertSchedule(
        db, "Scheduled shop", /*prompt*/"", /*skill*/"quick_shop", "{}",
        "at", "0", to_unix(Clock::now()) - 5, "chat");

    tickNow(proactive);

    // scheduled_goals row: at-kind disables itself and clears next_fire.
    int enabled = -1;
    int64_t last_fire = 0;
    db.query("SELECT enabled,last_fire FROM scheduled_goals WHERE id=?1", {sid},
             [&](const Row& r) { enabled = static_cast<int>(r.i64(0)); last_fire = r.i64(1); });
    assert(enabled == 0 && "kind=at schedule did not disable after firing");
    assert(last_fire > 0 && "last_fire not stamped");

    // A new goal was created, tagged origin="schedule".
    const int64_t after_goals = scalarCount(db, "SELECT COUNT(*) FROM goals");
    assert(after_goals == before_goals + 1 && "scheduled row did not create exactly one goal");

    int64_t gid = 0;
    std::string origin, context_json;
    db.query("SELECT id,origin,context_json FROM goals ORDER BY id DESC LIMIT 1", {},
             [&](const Row& r) { gid = r.i64(0); origin = r.text(1); context_json = r.text(2); });
    assert(origin == "schedule" && "scheduled goal not tagged origin=schedule");
    auto cj = nlohmann::json::parse(context_json, nullptr, false);
    assert(cj.is_object() && cj.value("schedule_id", int64_t{0}) == sid);

    // requestGoalExecution is QueuedConnection and often drains during tick's
    // event processing, so by the time we inspect steps the skill may already
    // have expanded (kind=skill → tool steps) and even finished. Accept either
    // the pre-expansion shape or the post-expansion trail; the real acceptance
    // bar is terminal done + the shopping side-effect.
    int64_t step_count = scalarCount(db, "SELECT COUNT(*) FROM plan_steps WHERE goal_id=?1", {gid});
    assert(step_count >= 1 && "scheduled goal has no plan_steps");
    bool saw_skill_or_shop = false;
    db.query("SELECT kind,tool FROM plan_steps WHERE goal_id=?1", {gid},
             [&](const Row& r) {
                 const std::string k = r.text(0);
                 const std::string t = r.isNull(1) ? "" : r.text(1);
                 if ((k == "skill" && t == "quick_shop") || t == "shopping_add")
                     saw_skill_or_shop = true;
             });
    assert(saw_skill_or_shop && "expected skill=quick_shop or expanded shopping_add step");

    // requestGoalExecution() launched it — it must reach a terminal state
    // (the skill's single shopping_add step is deterministic, no model needed).
    const bool terminal = waitForGoalTerminal(db, gid, 8000);
    assert(terminal && "scheduled goal never reached a terminal state");
    assert(goalStatus(db, gid) == "done" && "scheduled skill goal did not complete");
    assert(scalarCount(db,
              "SELECT COUNT(*) FROM shopping_items WHERE item='milk' AND done=0") == 1
           && "scheduled goal's skill step did not actually run");

    std::puts("  [ok] due scheduled_goals row (kind=skill,at) fires+executes a goal, then disables");
}

// ---------------------------------------------------------------------------
// 5. A due row (kind=prompt) produces the expected goal + plan_step shape.
// ---------------------------------------------------------------------------
void testPromptScheduleShape(ProactiveEngine& proactive, Database& db) {
    setQuietAlways(db, false);
    const int64_t sid = insertSchedule(
        db, "Morning briefing", "Give me my morning briefing", /*skill*/"", "{}",
        "at", "0", to_unix(Clock::now()) - 5, "voice");

    tickNow(proactive);

    int64_t gid = 0;
    std::string title, origin;
    db.query("SELECT id,title,origin FROM goals ORDER BY id DESC LIMIT 1", {},
             [&](const Row& r) { gid = r.i64(0); title = r.text(1); origin = r.text(2); });
    assert(title == "Morning briefing" && origin == "schedule");

    std::string step_kind, step_desc;
    db.query("SELECT kind,description FROM plan_steps WHERE goal_id=?1", {gid},
             [&](const Row& r) { step_kind = r.text(0); step_desc = r.text(1); });
    assert(step_kind == "prompt");
    assert(step_desc == "Give me my morning briefing");

    (void)sid;
    std::puts("  [ok] due scheduled_goals row (kind=prompt) creates the expected goal shape");
}

// ---------------------------------------------------------------------------
// 6. kind=every reschedules next_fire forward (stays enabled).
// ---------------------------------------------------------------------------
void testEveryReschedules(ProactiveEngine& proactive, Database& db) {
    setQuietAlways(db, false);
    const int64_t now = to_unix(Clock::now());
    const int64_t sid = insertSchedule(
        db, "Every 5 min", "", "quick_shop", "{}", "every", "300", now - 5, "chat");

    tickNow(proactive);

    int enabled = -1;
    int64_t next_fire = 0;
    db.query("SELECT enabled,next_fire FROM scheduled_goals WHERE id=?1", {sid},
             [&](const Row& r) { enabled = static_cast<int>(r.i64(0)); next_fire = r.i64(1); });
    assert(enabled == 1 && "kind=every schedule disabled itself (should stay enabled)");
    assert(next_fire > now && "kind=every schedule did not advance next_fire forward");

    std::puts("  [ok] due scheduled_goals row (kind=every) reschedules forward, stays enabled");
}

// ---------------------------------------------------------------------------
// 7. Quiet-hours gating: chat held, notify bypasses.
// ---------------------------------------------------------------------------
void testQuietHoursGating(ProactiveEngine& proactive, Database& db) {
    setQuietAlways(db, true);   // deterministic "always quiet" window
    const int64_t before_goals = scalarCount(db, "SELECT COUNT(*) FROM goals");
    const int64_t now = to_unix(Clock::now());

    const int64_t chat_sid = insertSchedule(
        db, "Held during quiet", "", "quick_shop", "{}", "at", "0", now - 5, "chat");
    const int64_t notify_sid = insertSchedule(
        db, "Bypasses quiet", "", "quick_shop", "{}", "at", "0", now - 5, "notify");

    tickNow(proactive);

    // chat: held — still enabled, next_fire untouched, no new goal for it.
    int chat_enabled = -1;
    db.query("SELECT enabled FROM scheduled_goals WHERE id=?1", {chat_sid},
             [&](const Row& r) { chat_enabled = static_cast<int>(r.i64(0)); });
    assert(chat_enabled == 1 && "deliver=chat schedule fired during quiet hours (should hold)");

    // notify: fired despite quiet hours — disabled (at-kind) + exactly one new goal.
    int notify_enabled = -1;
    db.query("SELECT enabled FROM scheduled_goals WHERE id=?1", {notify_sid},
             [&](const Row& r) { notify_enabled = static_cast<int>(r.i64(0)); });
    assert(notify_enabled == 0 && "deliver=notify schedule did not bypass quiet hours");

    const int64_t after_goals = scalarCount(db, "SELECT COUNT(*) FROM goals");
    assert(after_goals == before_goals + 1 &&
           "expected exactly one goal (from the notify row; chat row held)");

    // Now open quiet hours and confirm the held chat row fires on the next tick.
    setQuietAlways(db, false);
    tickNow(proactive);
    db.query("SELECT enabled FROM scheduled_goals WHERE id=?1", {chat_sid},
             [&](const Row& r) { chat_enabled = static_cast<int>(r.i64(0)); });
    assert(chat_enabled == 0 && "held deliver=chat schedule did not fire once quiet hours ended");

    std::puts("  [ok] quiet hours hold deliver=chat schedules; deliver=notify bypasses them");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // --- pure scheduler_util tests: no DB/services needed ---
    std::puts("test_scheduler_v2: sched_util pure-function checks");
    testAdvanceRruleBasic();
    testAdvanceEvery();
    testDstBoundary();

    // --- full-stack tests: ProactiveEngine -> AgentRuntime (A2 path) ---
    const auto root = std::filesystem::temp_directory_path() / "polymath_scheduler_v2";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    Paths::instance().setRoot(root);
    Paths::instance().ensureLayout();

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

    const auto dbPath = root / "scheduler_v2.db";
    Database db;
    assert(db.open(dbPath.string()));
    Config(db).seedDefaults();

    auto inf   = std::make_unique<InferenceManager>(db);
    auto sched = std::make_unique<TaskScheduler>(db, *inf);
    auto mem   = std::make_unique<MemoryService>(db, *inf);
    // Heap + leak on purpose (like test_goals.cpp): keeps teardown out of scope.
    auto* agent = new AgentRuntime(db, *inf, *sched, mem.get());
    agent->start();

    auto* proactive = new ProactiveEngine(db);
    proactive->start();

    std::puts("test_scheduler_v2: D1 full-stack checks (ProactiveEngine -> AgentRuntime)");
    testSkillScheduleFiresAndDisables(*proactive, db);
    testPromptScheduleShape(*proactive, db);
    testEveryReschedules(*proactive, db);
    testQuietHoursGating(*proactive, db);

    proactive->stop();
    agent->stop();
    // agent/proactive intentionally leaked at process exit.
    db.close();
    std::filesystem::remove_all(root, ec);
    std::puts("test_scheduler_v2: ALL CHECKS PASSED");
    return 0;
}
