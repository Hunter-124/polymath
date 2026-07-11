#pragma once
//
// ProactiveEngine — time/condition-based reminders (vitamins, dinner prep) and
// next-day suggestions.  Fires reminders via EventBus (TTS + UI), respecting
// quiet hours and presence (only nag when someone is home).
// The same 30s tick() also drives Scheduler v2 (overhaul2 D1): timed/
// recurring `scheduled_goals` rows fire real agent goals through the A2
// execution path (AgentRuntime::requestGoalExecution), tagged origin=schedule.
// IdleDetector — declares the machine "quiet" when there is no active
// conversation and the scene is calm, so the scheduler can run heavy work.
//
#include "service.h"
#include "event_bus.h"   // GoalUpdate (onGoalUpdated slot parameter)
#include <QObject>
#include <QTimer>
#include <cstdint>
#include <string>

namespace polymath {

class Database;

class ProactiveEngine : public QObject, public IService {
    Q_OBJECT
public:
    explicit ProactiveEngine(Database& db, QObject* parent = nullptr);
    void start() override;
    void stop() override;
    const char* serviceName() const override { return "proactive"; }

    qint64 addReminder(const std::string& text, qint64 due_unix,
                       const std::string& rrule = "", const std::string& condition = "");

private slots:
    void tick();   // periodic check for due reminders + due scheduled_goals
    // A schedule-originated goal reached a terminal state; speak it explicitly
    // when its deliver mode is "voice" (deliverGoalTerminal only auto-speaks
    // origin=="voice" goals, and scheduled goals are tagged origin="schedule").
    void onGoalUpdated(const polymath::GoalUpdate& g);

private:
    // A scheduled_goals row that is due to fire.
    struct DueSchedule {
        qint64      id = 0;
        std::string title;
        std::string prompt;
        std::string skill;
        std::string params_json;
        std::string kind;      // at | every | rrule
        std::string spec;
        std::string deliver;   // chat | voice | notify
        std::string source;
        int64_t     next_fire = 0;
    };

    bool inQuietHours() const;
    bool someoneHome() const;                       // recent person/face presence
    bool conditionMet(const std::string& cond) const;
    void fireReminder(qint64 id, const std::string& text,
                      const std::string& rrule, int64_t due_at);
    // D1: create a real goal for a due scheduled_goals row (mirrors run_skill's
    // own goal-creation shape), launch it, then reschedule/disable the row.
    void fireScheduledGoal(const DueSchedule& s);

    Database& db_;
    QTimer    timer_;
    int64_t   last_presence_unix_ = 0;              // refreshed by detection events
};

class IdleDetector : public QObject, public IService {
    Q_OBJECT
public:
    explicit IdleDetector(QObject* parent = nullptr);
    void start() override;
    void stop() override;
    const char* serviceName() const override { return "idle"; }
    void noteActivity();   // called on wake word / utterance / motion

signals:
    void idleChanged(bool idle);

private slots:
    void evaluate();

private:
    QTimer  timer_;
    qint64  last_activity_unix_ = 0;
    bool    idle_ = false;
};

} // namespace polymath
