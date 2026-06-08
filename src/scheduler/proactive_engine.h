#pragma once
//
// ProactiveEngine — time/condition-based reminders (vitamins, dinner prep) and
// next-day suggestions.  Fires reminders via EventBus (TTS + UI), respecting
// quiet hours and presence (only nag when someone is home).
// IdleDetector — declares the machine "quiet" when there is no active
// conversation and the scene is calm, so the scheduler can run heavy work.
//
#include "service.h"
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
    void tick();   // periodic check for due reminders

private:
    bool inQuietHours() const;
    bool someoneHome() const;                       // recent person/face presence
    bool conditionMet(const std::string& cond) const;
    void fireReminder(qint64 id, const std::string& text,
                      const std::string& rrule, int64_t due_at);

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
