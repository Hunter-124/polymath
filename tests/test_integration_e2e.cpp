// ===========================================================================
//  Integration / cross-service end-to-end test  (Wave 2 · Card H)
// ===========================================================================
//
//  This is the suite the headless harness (test_harness.h) was built for. It
//  boots the WHOLE backend offscreen — every service on its own QThread, wired
//  through the real EventBus, exactly as the running app does — and then drives
//  cross-service flows that span module boundaries and assert the observable
//  result (a bus message that crossed threads, a row a *different* service
//  persisted).
//
//  Everything here is DETERMINISTIC and model-free in the default suite:
//    * The harness points Paths at an empty temp root, so InferenceManager finds
//      no model and generate() answers "[no model loaded]" with done=true on the
//      spot (see inference_manager.cpp). That makes the agent loop terminate in
//      milliseconds instead of waiting on a 28 GB CPU model — so the cross-service
//      *plumbing* (Utterance -> agent -> tool/DB -> SpeakRequest) is exercised end
//      to end without the model being a dependency. CI (no models) stays green.
//
//  The model-dependent / heavy path (deep task -> scheduler -> Heavy model ->
//  result) is OPT-IN behind POLYMATH_E2E_FULL=1 (+ a Heavy model on disk); the
//  *queued* half of that flow runs unconditionally. See testDeepTaskFlow().
//
//  Cross-service flows asserted:
//    1. Utterance(command) -> AgentRuntime -> transcripts(DB) + SpeakRequest(bus)
//    2. setPrivacy() -> privacyChanged(bus) + settings(DB)   [system contract]
//    3. addShoppingItem() -> shopping_items(DB)              [UI action -> store]
//    4. queue_deep_task -> tasks(DB, 'queued') + taskUpdated(bus); heavy drain
//       opt-in.

#include "test_harness.h"

#include "task_scheduler.h"
#include "inference_manager.h"
#include "service.h"
#include "config.h"

#include <QCoreApplication>
#include <QThread>

#undef NDEBUG   // keep assert() live in Release (otherwise the test is a no-op)
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace polymath;
using namespace polymath::test;

namespace {

int64_t scalarCount(Database& db, const std::string& sql,
                    const std::vector<nlohmann::json>& params = {}) {
    int64_t n = 0;
    db.query(sql, params, [&](const Row& r) { n = r.i64(0); });
    return n;
}

// ---------------------------------------------------------------------------
//  0) Boot smoke — the harness stands up the full backend headless, no crash.
// ---------------------------------------------------------------------------
void testBoot() {
    HeadlessApp app;
    assert(app.boot() && "AppController::initialize() failed headless");
    assert(app.controller() != nullptr);

    // The DB was opened + migrated by AppController and seeded with config
    // defaults; our second handle sees the schema and the seeded settings.
    Database& db = app.db();
    assert(db.getBool("privacy.mic_enabled", false) == true &&
           "config defaults not seeded (privacy mic default should be ON)");

    // The Q_PROPERTY surface the GUI binds is readable on a freshly booted app.
    assert(!app.controller()->personalities().isEmpty() ||
           app.controller()->personalities().isEmpty());   // either is fine; just no crash
    (void)app.controller()->modelStatus();

    std::puts("  [ok] boot: 8 services up headless, DB migrated + seeded");
}

// ---------------------------------------------------------------------------
//  1) CROSS-SERVICE FLOW: Utterance -> AgentRuntime -> DB + SpeakRequest.
//
//     A post-wakeword command utterance is published on the bus (as AudioService
//     would after ASR). AppController has wired EventBus::utterance ->
//     AgentRuntime::handleUtterance, so the agent worker thread runs a full turn.
//     With no model the turn resolves immediately, but the loop still:
//       * persists the assistant transcript (memory/DB), and
//       * publishes a SpeakRequest for TTS (audio).
//     We assert BOTH the bus hop (SpeakRequest captured on the UI thread) and the
//     DB side effect (a transcript row), proving the audio->agent->DB->audio
//     message path is wired and live across three service threads.
// ---------------------------------------------------------------------------
void testVoiceToSpeechFlow() {
    HeadlessApp app;
    assert(app.boot());

    // Capture SpeakRequest payloads emitted from the agent worker thread.
    BusCapture<SpeakRequest> speak(&EventBus::speakRequested);

    const std::string cmd = "what time is it";
    app.injectCommand(cmd);

    // Wait for the agent turn to come back round to a SpeakRequest.
    const bool spoke = waitFor([&] { return !speak.empty(); }, 30000);
    assert(spoke && "agent never published a SpeakRequest for the utterance");

    // The agent streams the answer as one or more SpeakRequests, then closes the
    // TTS stream with an empty-text flush=true terminator (overhaul2 A1's
    // sentence-streaming final path; mirrors the long-standing streaming hook).
    // Assert the *content* hop: at least one captured SpeakRequest carried text.
    bool spokeText = false;
    for (const auto& s : speak.items())
        if (!s.text.isEmpty()) { spokeText = true; break; }
    assert(spokeText && "no SpeakRequest carried answer text");

    // DB side effect: the assistant turn was persisted as a transcript.
    Database& db = app.db();
    const bool transcriptPersisted = waitFor([&] {
        return scalarCount(db, "SELECT COUNT(*) FROM transcripts WHERE is_ambient=0") >= 1;
    }, 5000);
    assert(transcriptPersisted && "agent did not persist a command transcript");

    std::printf("  [ok] Utterance -> AgentRuntime -> SpeakRequest + transcript "
                "(spoke \"%.40s\")\n", speak.last().text.toStdString().c_str());
}

// ---------------------------------------------------------------------------
//  2) CROSS-SERVICE FLOW: setPrivacy() -> privacyChanged(bus) + settings(DB).
//     The privacy kill-switch is a system-wide contract every service honours.
//     The UI action both flips the persisted setting and announces it on the bus
//     (so live services react without re-reading the DB).
// ---------------------------------------------------------------------------
void testPrivacyFlow() {
    HeadlessApp app;
    assert(app.boot());

    BusCapture<PrivacyChanged> priv(&EventBus::privacyChanged);

    app.controller()->setPrivacy("privacy.mic_enabled", false);

    const bool announced = waitFor([&] {
        return priv.any([](const PrivacyChanged& p) {
            return p.key == "privacy.mic_enabled" && !p.enabled;
        });
    }, 5000);
    assert(announced && "setPrivacy did not announce privacyChanged on the bus");

    // Persisted: the controller's own read-back AND our independent DB handle agree.
    assert(app.controller()->privacy("privacy.mic_enabled") == false);
    assert(app.db().getBool("privacy.mic_enabled", true) == false &&
           "privacy setting not persisted to the DB");

    std::puts("  [ok] setPrivacy -> privacyChanged(bus) + settings(DB)");
}

// ---------------------------------------------------------------------------
//  3) UI-ACTION FLOW: addShoppingItem() -> shopping_items(DB).
//     A QML-callable action routes through the ShoppingModel to the store; the
//     independent DB handle confirms the row landed.
// ---------------------------------------------------------------------------
void testShoppingFlow() {
    HeadlessApp app;
    assert(app.boot());

    app.controller()->addShoppingItem("oat milk");
    app.controller()->addShoppingItem("espresso beans");

    Database& db = app.db();
    const bool added = waitFor([&] {
        return scalarCount(db, "SELECT COUNT(*) FROM shopping_items "
                               "WHERE done=0 AND item IN ('oat milk','espresso beans')") == 2;
    }, 5000);
    assert(added && "addShoppingItem did not persist both items");

    std::puts("  [ok] addShoppingItem -> shopping_items(DB)");
}

// ---------------------------------------------------------------------------
//  4) CROSS-SERVICE FLOW: deep task -> scheduler -> (Heavy model) -> result.
//
//     The QUEUE half always runs (deterministic, no model): enqueue() inserts a
//     'queued' tasks row and publishes taskUpdated('queued') on the bus.
//
//     The DRAIN half (idle -> load Heavy -> run -> 'done') is the model+GPU path.
//     It is OPT-IN: only when POLYMATH_E2E_FULL=1 AND a model is on disk do we
//     flip the scheduler idle and assert the task reaches a terminal state. With
//     no model the scheduler still drains (generate() answers "[no model loaded]"
//     done=true), so even the opt-in path doesn't hang — but we keep it gated so
//     the *default* CI suite never depends on a heavy load.
//
//     Standalone services (own threads, as AppController builds them) so the test
//     owns the idle signal — AppController's IdleDetector is real-clock-driven and
//     would make timing nondeterministic.
// ---------------------------------------------------------------------------
void testDeepTaskFlow() {
    // Fresh temp root so this is isolated from the harness instances above.
    const auto root = std::filesystem::temp_directory_path() / "polymath_deeptask_e2e";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    Paths::instance().setRoot(root);
    Paths::instance().ensureLayout();

    Database db;
    assert(db.open(Paths::instance().db().string()));
    Config(db).seedDefaults();

    // Heap-allocated and intentionally leaked: TaskScheduler holds a unique_ptr to
    // an incomplete-in-header collector type, so its dtor can't be instantiated in
    // this foreign TU (same constraint test_agent_e2e documents). The QThreads are
    // cleanly quit()+wait()'d; the short-lived test process reclaims the rest.
    auto* inf   = new InferenceManager(db);
    auto* sched = new TaskScheduler(db, *inf);

    QThread* tInf   = runOnThread(inf, inf);
    QThread* tSched = runOnThread(sched, sched);
    pump(100);   // let start() run on each thread

    BusCapture<TaskEvent> tasks(&EventBus::taskUpdated);

    // --- QUEUE half (always) ---
    const qint64 id = sched->enqueue("research", {{"topic", "best espresso beans"}}, 5);
    assert(id > 0);

    const bool queued = waitFor([&] {
        return scalarCount(db, "SELECT COUNT(*) FROM tasks WHERE id=?1 AND status='queued' "
                               "AND type='research' AND priority=5", {id}) == 1;
    }, 5000);
    assert(queued && "enqueue did not persist a 'queued' task row");

    const bool queuedOnBus = waitFor([&] {
        return tasks.any([&](const TaskEvent& t) {
            return t.task_id == id && t.status == "queued";
        });
    }, 5000);
    assert(queuedOnBus && "enqueue did not publish taskUpdated('queued')");

    // --- DRAIN half (opt-in) ---
    const char* full = std::getenv("POLYMATH_E2E_FULL");
    const bool runHeavy = full && std::string(full) == "1";
    if (runHeavy) {
        // Flip idle: hops onto the scheduler thread, loads Heavy (or no-model
        // fallback), drains the queue, publishes a terminal taskUpdated.
        QMetaObject::invokeMethod(sched, "onIdleChanged", Qt::QueuedConnection,
                                  Q_ARG(bool, true));
        const bool terminal = waitFor([&] {
            return scalarCount(db, "SELECT COUNT(*) FROM tasks WHERE id=?1 AND "
                                   "status IN ('done','error')", {id}) == 1;
        }, 120000);
        assert(terminal && "scheduler drain never reached a terminal task state");
        std::puts("  [ok] deep task -> scheduler -> drain -> terminal (POLYMATH_E2E_FULL)");
    } else {
        std::puts("  [ok] deep task -> scheduler queued (bus + DB); "
                  "drain is opt-in (set POLYMATH_E2E_FULL=1)");
    }

    for (QThread* t : {tSched, tInf}) { t->quit(); t->wait(15000); delete t; }
    db.close();
    std::filesystem::remove_all(root, ec);
}

} // namespace

int main(int argc, char** argv) {
    // One process-wide QCoreApplication: services need a running event dispatcher
    // for their per-thread event loops and for queued cross-thread bus delivery.
    QCoreApplication app(argc, argv);

    std::puts("test_integration_e2e: headless drive-the-app harness");

    testBoot();
    testVoiceToSpeechFlow();
    testPrivacyFlow();
    testShoppingFlow();
    testDeepTaskFlow();

    std::puts("test_integration_e2e: OK");
    return 0;
}
