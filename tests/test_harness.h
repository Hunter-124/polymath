#pragma once
// ---------------------------------------------------------------------------
//  Headless drive-the-app harness  (Wave 2 · Card H)
// ---------------------------------------------------------------------------
//
//  The reusable backbone the cross-service integration tests build on. It boots
//  the *real* backend (every service on its own QThread, wired through the real
//  EventBus) without the GUI — exactly as AppController::initialize() does for
//  the running app, but pointed at a throwaway temp root so a test never touches
//  the user's data/models.
//
//  What it gives a test:
//    * HeadlessApp app;  app.boot();   -> AppController up, 8 services running
//    * app.controller()                -> drive Q_INVOKABLE actions / read props
//    * app.db()                        -> a *second* read-only-ish Database handle
//                                         on the same on-disk file (SQLite WAL lets
//                                         the harness assert persisted state the
//                                         private AppController::db_ wrote)
//    * BusCapture                      -> subscribe to any EventBus signal and
//                                         record payloads for assertions
//    * pump(ms) / waitFor(pred, ms)    -> spin the Qt event loop so queued
//                                         cross-thread signals actually deliver
//
//  No live hardware is assumed: the temp root has no models, so InferenceManager
//  degrades to "no Fast model" (a warning, not a crash) and AudioService finds no
//  mic — the services still start, the bus still carries messages, the DB still
//  persists. Anything that genuinely needs a 28 GB model is gated opt-in by the
//  individual test, never by this harness.
//
//  Header-only on purpose: each integration TU includes it and links pm_app, so
//  there is exactly one AppController construction path (the app's own).

#include "app_controller.h"
#include "database.h"
#include "config.h"
#include "paths.h"
#include "event_bus.h"

#include <QCoreApplication>
#include <QObject>
#include <QEventLoop>
#include <QTimer>
#include <QDeadlineTimer>

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace polymath {
namespace test {

// --- event-loop helpers -----------------------------------------------------

// Spin the calling thread's Qt event loop for `ms`, delivering queued
// cross-thread EventBus signals. Use after injecting a message so the receiving
// service (on another thread) actually runs its slot.
inline void pump(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Spin until `pred()` is true or `timeout_ms` elapses. Returns pred()'s final
// value (true = satisfied, false = timed out). Polls the predicate between short
// event-loop slices so queued signals are delivered while we wait.
inline bool waitFor(const std::function<bool()>& pred, int timeout_ms) {
    QDeadlineTimer deadline(timeout_ms);
    while (!pred()) {
        if (deadline.hasExpired()) return pred();
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return true;
}

// --- generic EventBus payload recorder --------------------------------------

// Records every payload delivered on one EventBus signal, on the harness (UI)
// thread, so a test can assert what crossed the bus from a worker. One capture
// per signal; connect() is the member-function pointer of the signal.
template <typename Payload>
class BusCapture {
public:
    template <typename SignalT>
    BusCapture(SignalT signal) {
        conn_ = QObject::connect(&EventBus::instance(), signal,
                                 &sink_, [this](const Payload& p) {
                                     items_.push_back(p);
                                 });
    }
    ~BusCapture() { QObject::disconnect(conn_); }

    BusCapture(const BusCapture&) = delete;
    BusCapture& operator=(const BusCapture&) = delete;

    const std::vector<Payload>& items() const { return items_; }
    size_t count() const { return items_.size(); }
    bool   empty() const { return items_.empty(); }
    const Payload& last() const { return items_.back(); }

    // Number of recorded payloads for which `match` returns true.
    size_t countIf(const std::function<bool(const Payload&)>& match) const {
        size_t n = 0;
        for (const auto& p : items_) if (match(p)) ++n;
        return n;
    }
    bool any(const std::function<bool(const Payload&)>& match) const {
        return countIf(match) > 0;
    }

private:
    std::vector<Payload>     items_;
    QObject                  sink_;   // connection context; lives on this thread
    QMetaObject::Connection  conn_;
};

// --- the headless app fixture -----------------------------------------------

class HeadlessApp {
public:
    // `root` is the throwaway data dir all services read/write under. Defaults to
    // a unique temp dir; removed on destruction.
    explicit HeadlessApp(std::filesystem::path root = {}) {
        if (root.empty()) {
            // Unique per-construction so parallel test exes never collide.
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            root = std::filesystem::temp_directory_path() /
                   ("polymath_harness_" + std::to_string(stamp));
        }
        root_ = std::move(root);
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
        std::filesystem::create_directories(root_);
        // Point the whole process's path resolver at the temp root *before*
        // initialize() opens the DB / scans for models.
        Paths::instance().setRoot(root_);
        Paths::instance().ensureLayout();
    }

    ~HeadlessApp() {
        if (controller_) controller_->shutdown();
        controller_.reset();
        if (db_.raw()) db_.close();
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    HeadlessApp(const HeadlessApp&) = delete;
    HeadlessApp& operator=(const HeadlessApp&) = delete;

    // Stand up the controller exactly as main.cpp would (minus the QML engine).
    // Returns initialize()'s success. After this, all 8 services are on their
    // threads and the EventBus is live. Opens a second Database handle on the
    // same on-disk file for read-back assertions.
    bool boot() {
        controller_ = std::make_unique<AppController>();
        const bool ok = controller_->initialize();
        // Second handle on the same file (WAL => concurrent readers fine). Used to
        // assert what the services persisted through AppController's private db_.
        // AppController opens the DB with at-rest encryption when a SQLCipher codec
        // is linked, so our read-back handle must supply the SAME per-install key
        // (derived deterministically from the DPAPI keyfile initialize() created).
        const std::string key =
            Database::loadOrCreateKey((Paths::instance().root() / "db.key").string());
        db_.open(Paths::instance().db().string(), key);
        // Let each service's start() run on its thread (model warnings, etc.).
        pump(150);
        return ok;
    }

    AppController* controller() { return controller_.get(); }
    Database&      db() { return db_; }
    const std::filesystem::path& root() const { return root_; }

    // Convenience: publish a post-wakeword command utterance onto the bus, as
    // AudioService would after ASR. Wired in AppController to AgentRuntime +
    // TimelineModel.
    void injectCommand(const std::string& text) {
        Utterance u;
        u.text = text;
        u.is_ambient = false;
        u.confidence = 1.0f;
        u.ts = Clock::now();
        EventBus::instance().publishUtterance(u);
    }

private:
    std::filesystem::path           root_;
    std::unique_ptr<AppController>  controller_;
    Database                        db_;
};

} // namespace test
} // namespace polymath
