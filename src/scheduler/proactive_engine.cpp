#include "proactive_engine.h"
#include "scheduler_util.h"
#include "config.h"
#include "database.h"
#include "event_bus.h"
#include "logging.h"
#include "types.h"
// agent_runtime.h lives under src/agent, reachable via pm_scheduler's private
// include dir (src/scheduler/CMakeLists.txt already grants it — task_scheduler.cpp
// reaches into src/agent the same way for ToolRegistry). requestGoalExecution()
// is a free function resolved at final-binary link time (the app and any test
// exercising ProactiveEngine also link pm_agent), so pm_scheduler itself never
// needs to depend on pm_agent as a CMake target — avoids the pm_agent<->pm_scheduler
// library cycle (pm_agent already links pm_scheduler).
#include "agent_runtime.h"

#include <QUuid>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

// ProactiveEngine — time/condition-based reminders. Runs on its own QThread.
// tick() (every 30s) scans for due reminders, gates them on quiet hours +
// presence + any per-reminder condition, then fires reminderFired + a
// SpeakRequest and either marks the reminder fired (one-shot) or advances its
// recurrence (rrule).
//
// D1 (Scheduler v2): the same tick() also scans `scheduled_goals` for rows
// due to fire. A due row becomes a real `goals` row (origin="schedule", one
// plan_step of kind "skill" or "prompt" — the same shapes run_skill/AgentLoop
// already know how to execute), handed to the live AgentRuntime via
// requestGoalExecution() (A2's execution path: full plan/execute/reflect +
// tools). Results are delivered through the existing goal-terminal path
// (AgentLoop::deliverGoalTerminal — notice + chat transcript on every
// terminal goal, plus TTS for origin=="voice"); since scheduled goals are
// tagged origin="schedule" (not "voice"), onGoalUpdated() below adds the extra
// spoken echo for rows whose `deliver` is "voice".

namespace polymath {

namespace {

// A reminder row that is due.
struct DueReminder {
    qint64      id = 0;
    std::string text;
    int64_t     due_at = 0;
    std::string rrule;
    std::string condition;
};

// How recently a person/face event counts as "someone is home".
constexpr int64_t kPresenceWindowSec = 10 * 60;   // 10 minutes

} // namespace

ProactiveEngine::ProactiveEngine(Database& db, QObject* parent) : QObject(parent), db_(db) {}

void ProactiveEngine::start() {
    PM_INFO("ProactiveEngine started");

    // Track live presence: any person/face detection refreshes the timestamp so
    // we can decide whether it's worth nagging. Queued by default (cross-thread).
    connect(&EventBus::instance(), &EventBus::detection, this,
            [this](const Detection& d) {
                for (const auto& b : d.boxes) {
                    if (b.label == "person" || b.label == "face") {
                        last_presence_unix_ = to_unix(Clock::now());
                        break;
                    }
                }
            });

    // D1: explicit spoken echo for deliver=="voice" scheduled goals (queued —
    // AgentLoop's goalUpdated emission happens on the agent worker thread).
    connect(&EventBus::instance(), &EventBus::goalUpdated,
            this, &ProactiveEngine::onGoalUpdated);

    connect(&timer_, &QTimer::timeout, this, &ProactiveEngine::tick);
    timer_.start(30'000);
    tick();   // run once on startup so freshly-due reminders don't wait 30s
}

void ProactiveEngine::stop() {
    timer_.stop();
    PM_INFO("ProactiveEngine stopped");
}

qint64 ProactiveEngine::addReminder(const std::string& text, qint64 due,
                                    const std::string& rrule, const std::string& cond) {
    // due <= 0 means "no scheduled time" -> store NULL so the reminder is purely
    // condition-driven (schema: due_at NULL => condition-based).
    nlohmann::json due_param = (due > 0) ? nlohmann::json(static_cast<int64_t>(due))
                                         : nlohmann::json(nullptr);
    qint64 id = db_.exec("INSERT INTO reminders(text,due_at,rrule,condition,created_at)"
                         " VALUES(?1,?2,?3,?4,?5)",
                         {text, due_param, rrule, cond, to_unix(Clock::now())});
    PM_INFO("ProactiveEngine: added reminder {} due_at={} rrule='{}' cond='{}'",
            id, due, rrule, cond);
    return id;
}

bool ProactiveEngine::inQuietHours() const {
    Config cfg(db_);
    const int start = sched_util::parseHhMm(cfg.getStr(keys::QuietHoursStart, "22:00"));
    const int end   = sched_util::parseHhMm(cfg.getStr(keys::QuietHoursEnd, "07:00"));
    return sched_util::inWindow(to_unix(Clock::now()), start, end);
}

bool ProactiveEngine::someoneHome() const {
    const int64_t now = to_unix(Clock::now());
    // Live signal first (refreshed by the detection subscription).
    if (last_presence_unix_ > 0 && now - last_presence_unix_ <= kPresenceWindowSec)
        return true;
    // Fall back to the persisted events log (e.g. just after startup).
    bool recent = false;
    db_.query("SELECT 1 FROM events WHERE kind IN ('person','face') AND ts >= ?1 LIMIT 1",
              {now - kPresenceWindowSec},
              [&](const Row&) { recent = true; });
    return recent;
}

bool ProactiveEngine::conditionMet(const std::string& condition) const {
    if (condition.empty()) return true;
    if (condition == "someone_home" || condition == "presence")
        return someoneHome();
    if (condition == "always")
        return true;
    // Unknown condition: be conservative and require presence so we don't talk
    // to an empty room, but still allow firing when someone is around.
    PM_WARN("ProactiveEngine: unknown condition '{}', gating on presence", condition);
    return someoneHome();
}

void ProactiveEngine::tick() {
    const int64_t now = to_unix(Clock::now());
    const bool quiet = inQuietHours();

    // --- reminders (existing) -----------------------------------------------
    // Pull due, not-yet-fired, time-based reminders. (condition-only reminders
    // have NULL due_at and are evaluated continuously below.)
    std::vector<DueReminder> due;
    db_.query(
        "SELECT id,text,due_at,rrule,condition FROM reminders "
        "WHERE fired=0 AND ((due_at IS NOT NULL AND due_at<=?1) "
        "                   OR (due_at IS NULL AND condition<>''))",
        {now},
        [&](const Row& r) {
            DueReminder d;
            d.id        = r.i64(0);
            d.text      = r.text(1);
            d.due_at    = r.isNull(2) ? 0 : r.i64(2);
            d.rrule     = r.text(3);
            d.condition = r.text(4);
            due.push_back(std::move(d));
        });

    for (const auto& d : due) {
        if (!conditionMet(d.condition)) {
            PM_DEBUG("ProactiveEngine: reminder {} held (condition '{}' not met)",
                     d.id, d.condition);
            continue;   // re-checked next tick; not marked fired
        }
        if (quiet) {
            // Respect quiet hours: hold the reminder (leave fired=0) so it fires
            // once the window ends. Recurring reminders also wait.
            PM_DEBUG("ProactiveEngine: reminder {} held (quiet hours)", d.id);
            continue;
        }
        fireReminder(d.id, d.text, d.rrule, d.due_at);
    }

    // --- scheduled_goals (D1: Scheduler v2) ----------------------------------
    std::vector<DueSchedule> due_sched;
    db_.query(
        "SELECT id,title,prompt,skill,params_json,kind,spec,next_fire,deliver,source "
        "FROM scheduled_goals WHERE enabled=1 AND next_fire IS NOT NULL AND next_fire<=?1",
        {now},
        [&](const Row& r) {
            DueSchedule s;
            s.id          = r.i64(0);
            s.title       = r.text(1);
            s.prompt      = r.text(2);
            s.skill       = r.text(3);
            s.params_json = r.text(4);
            s.kind        = r.text(5);
            s.spec        = r.text(6);
            s.next_fire   = r.isNull(7) ? 0 : r.i64(7);
            s.deliver     = r.text(8);
            s.source      = r.text(9);
            due_sched.push_back(std::move(s));
        });

    for (const auto& s : due_sched) {
        // Quiet hours hold the schedule (retried next tick) unless the row is
        // marked deliver=="notify" — a silent notification doesn't disturb
        // quiet hours the way a chat line / spoken briefing would.
        if (quiet && s.deliver != "notify") {
            PM_DEBUG("ProactiveEngine: scheduled goal {} held (quiet hours)", s.id);
            continue;
        }
        fireScheduledGoal(s);
    }
}

void ProactiveEngine::fireReminder(qint64 id, const std::string& text,
                                   const std::string& rrule, int64_t due_at) {
    PM_INFO("ProactiveEngine: firing reminder {}: {}", id, text);

    EventBus::instance().publishReminder({id, QString::fromStdString(text)});

    SpeakRequest say;
    say.text = QString::fromStdString(text);
    say.voice = "";   // active personality voice resolved downstream by TTS
    say.request_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    EventBus::instance().publishSpeak(say);

    const int64_t now = to_unix(Clock::now());

    // Recurrence: advance due_at to the next occurrence and keep fired=0.
    if (!rrule.empty()) {
        int64_t base = due_at > 0 ? due_at : now;
        int64_t next = sched_util::advanceRrule(rrule, base);
        // Skip past any occurrences already in the past (e.g. app was off).
        int guard = 0;
        while (next > 0 && next <= now && guard++ < 100'000)
            next = sched_util::advanceRrule(rrule, next);
        if (next > 0) {
            db_.exec("UPDATE reminders SET due_at=?2 WHERE id=?1", {id, next});
            PM_INFO("ProactiveEngine: reminder {} recurs, next due_at={}", id, next);
            return;
        }
        PM_WARN("ProactiveEngine: reminder {} has rrule '{}' but could not advance; "
                "marking fired", id, rrule);
    }

    // One-shot (or unparseable recurrence): mark fired.
    db_.exec("UPDATE reminders SET fired=1 WHERE id=?1", {id});
}

// D1: fire one due scheduled_goals row — create a real goal (same shape
// run_skill persists: a goal + a single plan_step), hand it to the live
// AgentRuntime via requestGoalExecution() (A2's execution path — queued, runs
// on the agent worker thread, never re-entrant), then reschedule/disable.
void ProactiveEngine::fireScheduledGoal(const DueSchedule& s) {
    const int64_t now = to_unix(Clock::now());
    const std::string title = s.title.empty() ? "Scheduled task" : s.title;

    nlohmann::json context = {
        {"request_id", "schedule:" + std::to_string(s.id)},
        {"schedule_id", s.id},
        {"deliver", s.deliver},
        {"trace", nlohmann::json::array()},
    };
    const int64_t goal_id = db_.exec(
        "INSERT INTO goals(title,status,origin,context_json,created_at,updated_at) "
        "VALUES(?1,'active','schedule',?2,?3,?3)",
        {title, context.dump(), now});
    if (goal_id < 0) {
        PM_ERROR("ProactiveEngine: failed to create goal for scheduled_goals {}", s.id);
        return;
    }

    // One step: a skill (AgentLoop::dispatchSkillStep expands it inline, same
    // as run_skill) or a free-form prompt (AgentLoop::dispatchPromptStep runs
    // it through the model — the "give me a briefing" shape).
    std::string kind, tool, description;
    nlohmann::json step_args = nlohmann::json::object();
    if (!s.skill.empty()) {
        kind = "skill";
        tool = s.skill;
        description = "Run skill " + s.skill;
        nlohmann::json params = nlohmann::json::parse(s.params_json, nullptr, false);
        if (!params.is_object()) params = nlohmann::json::object();
        step_args = {{"name", s.skill}, {"params", params}};
    } else {
        kind = "prompt";
        description = s.prompt.empty() ? title : s.prompt;
    }

    db_.exec(
        "INSERT INTO plan_steps(goal_id,idx,description,kind,tool,args_json,"
        "status,attempts,updated_at) VALUES(?1,0,?2,?3,?4,?5,'pending',0,?6)",
        {goal_id, description, kind,
         tool.empty() ? nlohmann::json(nullptr) : nlohmann::json(tool),
         step_args.dump(), now});

    PM_INFO("ProactiveEngine: fired scheduled goal={} schedule={} kind={} deliver={}",
            goal_id, s.id, s.kind, s.deliver);
    requestGoalExecution(goal_id);

    db_.exec("UPDATE scheduled_goals SET last_fire=?2 WHERE id=?1", {s.id, now});

    if (s.kind == "at") {
        db_.exec("UPDATE scheduled_goals SET enabled=0, next_fire=NULL WHERE id=?1", {s.id});
        return;
    }
    if (s.kind == "every") {
        int64_t interval = 0;
        try { interval = std::stoll(s.spec); } catch (...) {}
        int64_t next = sched_util::advanceEvery(s.next_fire > 0 ? s.next_fire : now, interval);
        int guard = 0;
        while (next > 0 && next <= now && guard++ < 100'000)
            next = sched_util::advanceEvery(next, interval);
        if (next > 0) {
            db_.exec("UPDATE scheduled_goals SET next_fire=?2 WHERE id=?1", {s.id, next});
            return;
        }
        PM_WARN("ProactiveEngine: scheduled goal {} bad every-interval '{}'; disabling",
                s.id, s.spec);
        db_.exec("UPDATE scheduled_goals SET enabled=0, next_fire=NULL WHERE id=?1", {s.id});
        return;
    }
    // rrule
    int64_t next = sched_util::advanceRrule(s.spec, s.next_fire);
    int guard = 0;
    while (next > 0 && next <= now && guard++ < 100'000)
        next = sched_util::advanceRrule(s.spec, next);
    if (next > 0) {
        db_.exec("UPDATE scheduled_goals SET next_fire=?2 WHERE id=?1", {s.id, next});
        return;
    }
    PM_WARN("ProactiveEngine: scheduled goal {} rrule '{}' could not advance; disabling",
            s.id, s.spec);
    db_.exec("UPDATE scheduled_goals SET enabled=0, next_fire=NULL WHERE id=?1", {s.id});
}

// deliverGoalTerminal (agent_loop.cpp) already publishes a Notice + persists a
// chat transcript line for EVERY terminal goal, and speaks it when
// goal.origin=="voice". Scheduled goals are tagged origin="schedule" (so the
// generic path never auto-speaks them); this adds the explicit spoken echo for
// rows whose `deliver` is "voice", without touching agent_loop.cpp (owned by
// A2/A4/D2).
void ProactiveEngine::onGoalUpdated(const GoalUpdate& g) {
    const std::string status = g.status.toStdString();
    if (status != "done" && status != "failed" && status != "cancelled")
        return;   // only terminal states are "results"

    bool ok = false;
    const int64_t goal_id = g.goal_id.toLongLong(&ok);
    if (!ok || goal_id <= 0) return;

    std::string context_json, origin;
    db_.query("SELECT context_json,origin FROM goals WHERE id=?1", {goal_id},
              [&](const Row& r) { context_json = r.text(0); origin = r.text(1); });
    if (origin != "schedule") return;   // not one of ours

    auto cj = nlohmann::json::parse(context_json, nullptr, false);
    if (!cj.is_object() || cj.value("deliver", "chat") != "voice") return;

    const QString line = QStringLiteral("%1 Finished: %2 — %3")
                             .arg(status == "done" ? QStringLiteral("✔")
                                                    : QStringLiteral("✖"))
                             .arg(g.title, g.summary);
    SpeakRequest say;
    say.text = line;
    say.voice = "";   // active personality voice resolved downstream by TTS
    say.request_id = QStringLiteral("schedule-speak:%1").arg(g.goal_id);
    EventBus::instance().publishSpeak(say);
}

} // namespace polymath
