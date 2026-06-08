#include "proactive_engine.h"
#include "scheduler_util.h"
#include "config.h"
#include "database.h"
#include "event_bus.h"
#include "logging.h"
#include "types.h"

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

    if (due.empty()) return;

    const bool quiet = inQuietHours();

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

} // namespace polymath
