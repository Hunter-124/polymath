#include "proactive_engine.h"   // declares IdleDetector
#include "event_bus.h"
#include "logging.h"
#include "types.h"

// IdleDetector — declares the machine "quiet" when there has been no wake word,
// utterance, or motion/person detection for kIdleThresholdSec. Subscribes the
// relevant EventBus signals to noteActivity() and emits idleChanged on each
// transition so the TaskScheduler can run heavy work only when nobody's around.

namespace polymath {

namespace {
// Minutes of silence before we consider the machine idle. Kept here (not the
// frozen Config keys) since it's a scheduler-internal heuristic.
constexpr int64_t kIdleThresholdSec = 5 * 60;   // 5 minutes
constexpr int     kEvaluateIntervalMs = 10'000; // re-check every 10s
} // namespace

IdleDetector::IdleDetector(QObject* parent) : QObject(parent) {}

void IdleDetector::start() {
    PM_INFO("IdleDetector started (threshold {}s)", kIdleThresholdSec);
    last_activity_unix_ = to_unix(Clock::now());

    // Any of these counts as the human being present/active. Queued connections
    // (cross-thread) marshal onto this thread; noteActivity only touches
    // last_activity_unix_, which is also read on this thread in evaluate().
    auto& bus = EventBus::instance();
    connect(&bus, &EventBus::wakeWord, this,
            [this](const WakeWordEvent&) { noteActivity(); });
    connect(&bus, &EventBus::utterance, this,
            [this](const Utterance&) { noteActivity(); });
    connect(&bus, &EventBus::detection, this,
            [this](const Detection& d) {
                // Motion or any person/face box is "activity"; ignore empty
                // detections (a detector heartbeat with no boxes).
                if (!d.boxes.empty()) noteActivity();
            });

    connect(&timer_, &QTimer::timeout, this, &IdleDetector::evaluate);
    timer_.start(kEvaluateIntervalMs);
}

void IdleDetector::stop() {
    timer_.stop();
    PM_INFO("IdleDetector stopped");
}

void IdleDetector::noteActivity() {
    last_activity_unix_ = to_unix(Clock::now());
    if (idle_) {
        idle_ = false;
        PM_INFO("IdleDetector: activity — no longer idle");
        emit idleChanged(false);
    }
}

void IdleDetector::evaluate() {
    const int64_t now = to_unix(Clock::now());
    const bool quiet = (now - last_activity_unix_) >= kIdleThresholdSec;
    if (quiet && !idle_) {
        idle_ = true;
        PM_INFO("IdleDetector: {}s of inactivity — idle", now - last_activity_unix_);
        emit idleChanged(true);
    } else if (!quiet && idle_) {
        // Defensive: noteActivity() normally handles the falling edge first.
        idle_ = false;
        emit idleChanged(false);
    }
}

} // namespace polymath
