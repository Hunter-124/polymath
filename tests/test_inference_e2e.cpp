// Integration test for Card D — tiered inference + scheduler.
//
// Tiers (cheapest first so the default ctest stays fast & green):
//   1. testBudgetMath  — pure VramBudget math: full-fit / partial-offload /
//      eviction+restore / no-headroom→CPU. Always runs, no GPU, no model.
//   2. testGrammar     — buildToolCallGrammar() emits parseable GBNF, and (when
//      llama.cpp is compiled in) llama_sampler_init_grammar() accepts it. This
//      is the direct regression guard for the 0xC0000409 grammar crash: a bad
//      grammar would make init return null here.
//   3. testLifecycle   — InferenceManager Fast→Heavy→Fast load/unload against the
//      real Fast GGUF if one is on disk. Skipped when no model dir is present, or
//      when POLYMATH_SKIP_MODEL_E2E=1.
//   4. testFullDrain   — the headline Fast→idle→Heavy→drain a queued deep task→
//      Fast cycle through TaskScheduler + a forced-idle signal. Heavy (27B) is
//      large and slow, so this is OPT-IN via POLYMATH_E2E_FULL=1. The CI default
//      stays green without holding the shared GPU.
//
// Build: linked against pm_core + pm_inference + pm_scheduler (see CMakeLists).

#include "vram_budget.h"
#include "grammar.h"
#include "llama_backend.h"
#include "inference_manager.h"
#include "task_scheduler.h"
#include "proactive_engine.h"   // IdleDetector
#include "database.h"
#include "paths.h"
#include "event_bus.h"
#include "types.h"

#undef NDEBUG   // keep assert() live in Release, otherwise this test is a no-op
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <QCoreApplication>
#include <QThread>
#include <QEventLoop>
#include <QTimer>

using namespace polymath;
namespace fs = std::filesystem;

static bool envOn(const char* k) {
    const char* v = std::getenv(k);
    return v && *v && std::string(v) != "0";
}

// ---------------------------------------------------------------------------
// 1) VRAM budget math — the load-bearing planGpuLayers / reserve / release logic.
// ---------------------------------------------------------------------------
static void testBudgetMath() {
    std::puts("[budget] planGpuLayers + reserve/release");

    // A 12 GB-class device modelled via the no-CUDA fallback path: total ==
    // budget, free == budget - reserved. (On a CUDA build this still exercises
    // the same arithmetic; the live free pool only makes it *more* conservative.)
    VramBudget vb(/*budgetMiB*/ 8192);

    // Full fit: a small model with generous headroom offloads every layer.
    {
        const int planned = vb.planGpuLayers(/*modelMiB*/ 2000, /*layers*/ 32);
        assert(planned == 32);
    }

    // Partial offload: a model bigger than the headroom gets a fractional layer
    // count strictly between 0 and the total (the "degrade onto CPU" path).
    {
        const int planned = vb.planGpuLayers(/*modelMiB*/ 16000, /*layers*/ 62);
        assert(planned > 0 && planned < 62);
    }

    // Eviction accounting: once Fast is reserved, the budget left shrinks, so the
    // *same* heavy model now plans fewer layers than with an empty ledger.
    {
        const int before = vb.planGpuLayers(16000, 62);
        vb.reserve("fast-model", 5000);
        assert(vb.reservedMiB() == 5000);
        assert(vb.reservedFor("fast-model") == 5000);
        const int after = vb.planGpuLayers(16000, 62);
        assert(after < before);          // less room => fewer offloaded layers

        // Restore: releasing Fast returns the headroom (the requestHeavy(false)
        // path that lets Fast become resident again).
        vb.release("fast-model");
        assert(vb.reservedMiB() == 0);
        const int restored = vb.planGpuLayers(16000, 62);
        assert(restored == before);
    }

    // No headroom => 0 layers => the model runs entirely on CPU (correct, slow).
    {
        vb.reserve("hog", 8192);         // consume the whole budget
        const int planned = vb.planGpuLayers(16000, 62);
        assert(planned == 0);
        vb.release("hog");
    }

    // estimateModelMiB never returns 0 (unknown path still budgets a footprint).
    {
        const size_t est = VramBudget::estimateModelMiB("does-not-exist.gguf", 4096);
        assert(est > 0);
    }

    std::puts("[budget] OK");
}

// ---------------------------------------------------------------------------
// 2) Grammar: emit a tool-call GBNF and prove it is well-formed. When the real
//    engine is linked, run an actual grammar-CONSTRAINED decode on the Fast GGUF
//    through LlamaBackend — the direct regression for the 0xC0000409 fast-fail
//    card B hit (gemma-3n + a GBNF crashed inside the sampler). We drive only the
//    public LlamaBackend API so the test needs no llama.cpp headers/linkage.
// ---------------------------------------------------------------------------
static void testGrammar(const fs::path& modelsRoot) {
    std::puts("[grammar] buildToolCallGrammar");

    using grammar::ToolDef;
    std::vector<ToolDef> tools = {
        {"add_reminder", nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"text",     {{"type", "string"}}},
                {"when",     {{"type", "string"}}},
                {"priority", {{"type", "integer"}}},
                {"repeat",   {{"type", "boolean"}}},
            }},
        }},
        {"search", nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}}},
                {"limit", {{"type", "integer"}}},
            }},
        }},
    };

    const std::string gbnf = grammar::buildToolCallGrammar(tools);
    assert(!gbnf.empty());
    assert(gbnf.find("root ::=") != std::string::npos);
    assert(gbnf.find("\\\"tool\\\"") != std::string::npos);   // pins {"tool":...}

    // Empty tool list => no grammar (caller samples unconstrained).
    assert(grammar::buildToolCallGrammar({}).empty());

#ifdef POLYMATH_INFERENCE_HAS_LLAMA
    // Run a REAL grammar-constrained decode on the Fast GGUF (gemma-3n by default
    // — the exact model card B crashed on). Skipped if no model dir, or via
    // POLYMATH_SKIP_GRAMMAR_DECODE. This is the load-bearing crash regression.
    if (envOn("POLYMATH_SKIP_GRAMMAR_DECODE")) {
        std::puts("[grammar] constrained decode skipped (POLYMATH_SKIP_GRAMMAR_DECODE)");
        std::puts("[grammar] OK");
        return;
    }
    fs::path fast;
    std::error_code ec;
    if (!modelsRoot.empty() && fs::exists(modelsRoot / "llm", ec)) {
        // Prefer the gemma-3n / E4B Fast model (card B's crash repro); else take
        // the first non-projector, non-27B/32B/70B gguf as the Fast model.
        fs::path firstSmall;
        for (auto& e : fs::directory_iterator(modelsRoot / "llm", ec)) {
            const std::string n = e.path().filename().string();
            if (e.path().extension() != ".gguf" || n.find("mmproj") != std::string::npos)
                continue;
            const bool big = n.find("27b") != std::string::npos ||
                             n.find("27B") != std::string::npos ||
                             n.find("32b") != std::string::npos ||
                             n.find("70b") != std::string::npos;
            if (n.find("3n") != std::string::npos || n.find("E4B") != std::string::npos) {
                fast = e.path(); break;
            }
            if (!big && firstSmall.empty()) firstSmall = e.path();
        }
        if (fast.empty()) fast = firstSmall;
    }
    if (fast.empty()) {
        std::puts("[grammar] (no Fast GGUF on disk; skipped constrained-decode check)");
        std::puts("[grammar] OK");
        return;
    }

    LlamaBackend backend;
    ModelSpec spec;
    spec.id = fast.stem().string();
    spec.path = fast.string();
    spec.role = ModelRole::Fast;
    spec.n_ctx = 2048;
    spec.n_gpu_layers = envOn("POLYMATH_E2E_GPU") ? 999 : 0;   // CPU-safe default
    assert(backend.load(spec) && "Fast model failed to load for grammar decode");

    ChatRequest req;
    req.request_id = "grammar-decode";
    req.sampling.grammar = gbnf;            // constrain output to a tool call
    req.sampling.max_tokens = 96;
    req.sampling.temperature = 0.0f;        // deterministic
    req.messages.push_back({Role::System, "You can only respond by calling a tool as JSON."});
    req.messages.push_back({Role::User,   "Remind me to take vitamins at 9am tomorrow."});

    std::string out;
    backend.generate(req, [&](std::string_view piece, bool) {
        out.append(piece.data(), piece.size());
    });
    backend.unload();

    std::printf("[grammar] constrained decode produced %zu chars: %.120s\n",
                out.size(), out.c_str());
    // Reaching here at all is the headline: the old code fast-failed (0xC0000409)
    // on the first constrained decode. Also check the grammar actually bit.
    assert(!out.empty() && "constrained decode produced no output");
    assert(out.find("\"tool\"") != std::string::npos &&
           "grammar did not constrain output to a {\"tool\":...} object");
    try {
        auto j = nlohmann::json::parse(out);
        assert(j.contains("tool") && j.contains("arguments"));
    } catch (const std::exception& e) {
        std::printf("[grammar] WARN: output not strict JSON: %s\n", e.what());
    }
    std::puts("[grammar] constrained decode OK — no 0xC0000409 crash");
#else
    (void)modelsRoot;
#endif
    std::puts("[grammar] OK");
}

// Spin the calling thread's event loop until `pred` is true or `ms` elapses.
template <class Pred>
static bool waitFor(Pred pred, int ms) {
    QEventLoop loop;
    QTimer poll; poll.setInterval(20);
    bool ok = false;
    QObject::connect(&poll, &QTimer::timeout, [&] {
        if (pred()) { ok = true; loop.quit(); }
    });
    QTimer::singleShot(ms, [&] { loop.quit(); });
    poll.start();
    loop.exec();
    return ok || pred();
}

// Locate a usable models dir: prefer the test exe's data/models, else fall back
// to the junctioned CPU build tree two levels up. Returns "" if none found.
static fs::path findModelsRoot() {
    std::error_code ec;
    const fs::path exeDir = fs::path(QCoreApplication::applicationDirPath().toStdString());
    for (const fs::path cand : {
            exeDir / "data" / "models",
            exeDir / ".." / ".." / "bin" / "Release" / "data" / "models",
        }) {
        if (fs::exists(cand / "llm", ec)) return fs::weakly_canonical(cand, ec);
    }
    return {};
}

// ---------------------------------------------------------------------------
// 3) Lifecycle: InferenceManager Fast→Heavy→Fast against the real GGUF on disk.
// ---------------------------------------------------------------------------
static bool testLifecycle(const fs::path& modelsRoot) {
    if (envOn("POLYMATH_SKIP_MODEL_E2E")) {
        std::puts("[lifecycle] skipped (POLYMATH_SKIP_MODEL_E2E)");
        return true;
    }
    if (modelsRoot.empty()) {
        std::puts("[lifecycle] skipped (no models dir found)");
        return true;
    }
    std::puts("[lifecycle] Fast -> Heavy -> Fast load/unload");
    Paths::instance().setRoot(modelsRoot.parent_path());   // root/<models>

    // Fresh temp DB so autoDiscoverModels() registers the on-disk GGUFs.
    const fs::path dbPath = fs::temp_directory_path() / "polymath_e2e_inf.db";
    std::error_code ec; fs::remove(dbPath, ec);
    Database db;
    assert(db.open(dbPath.string()));

    InferenceManager inf(db);
    inf.start();                                   // discovers + loads active Fast

    const auto reg = inf.registry();
    assert(!reg.empty() && "no models registered from disk");
    bool haveFast = false, haveHeavy = false;
    for (const auto& s : reg) {
        if (s.role == ModelRole::Fast)  haveFast = true;
        if (s.role == ModelRole::Heavy) haveHeavy = true;
    }
    assert(haveFast && "expected a Fast model on disk");

    // Fast must be resident after start().
    assert(!inf.heavyLoaded());

    if (haveHeavy && envOn("POLYMATH_E2E_FULL")) {
        // Only exercise the (heavy, VRAM-hungry) Heavy swap when explicitly asked
        // — keeps the default run from loading the 27B model.
        inf.requestHeavy(true);
        assert(inf.heavyLoaded() && "Heavy failed to load");
        inf.requestHeavy(false);
        assert(!inf.heavyLoaded() && "Heavy failed to unload");
        std::puts("[lifecycle] Heavy swap exercised");
    } else {
        std::puts("[lifecycle] Heavy swap skipped (set POLYMATH_E2E_FULL=1 to run)");
    }

    inf.stop();
    db.close();
    fs::remove(dbPath, ec);
    std::puts("[lifecycle] OK");
    return true;
}

// ---------------------------------------------------------------------------
// 4) Full drain: Fast → forced idle → Heavy → run a queued deep task → result
//    persists in `tasks` → Heavy unloads → Fast resident again. OPT-IN (slow).
// ---------------------------------------------------------------------------
static bool testFullDrain(const fs::path& modelsRoot) {
    if (!envOn("POLYMATH_E2E_FULL")) {
        std::puts("[drain] skipped (set POLYMATH_E2E_FULL=1 to run the full cycle)");
        return true;
    }
    if (modelsRoot.empty()) { std::puts("[drain] skipped (no models)"); return true; }
    std::puts("[drain] Fast -> idle -> Heavy -> drain -> Fast");
    Paths::instance().setRoot(modelsRoot.parent_path());

    const fs::path dbPath = fs::temp_directory_path() / "polymath_e2e_drain.db";
    std::error_code ec; fs::remove(dbPath, ec);
    Database db; assert(db.open(dbPath.string()));

    // Run the two services on their OWN QThreads, exactly like AppController does.
    // This is required: TaskScheduler::drainQueue blocks (StreamCollector waits on
    // tokens delivered from the inference thread), so the scheduler must have its
    // own event loop separate from the inference one, or the wait deadlocks.
    auto* inf = new InferenceManager(db);
    auto* sched = new TaskScheduler(db, *inf);
    auto* idle = new IdleDetector();

    QThread* infThread   = runOnThread(inf, inf);     // start()s on its thread
    QThread* schedThread = runOnThread(sched, sched);
    QObject::connect(idle, &IdleDetector::idleChanged,
                     sched, &TaskScheduler::onIdleChanged, Qt::QueuedConnection);

    // Wait for the Fast model to come resident on the inference thread.
    assert(waitFor([&] { return !inf->registry().empty(); }, 60'000)
           && "inference never started");

    // Record completion via the scheduler's signal (queued back to this thread).
    std::atomic<bool> finished{false};
    qint64 finishedId = 0;
    QObject::connect(sched, &TaskScheduler::taskFinished, sched,
                     [&](qint64 tid, const QString&) { finishedId = tid; finished = true; },
                     Qt::QueuedConnection);

    // Queue a small deep task. enqueue() is documented thread-safe (only touches
    // the thread-safe DB + posts a queued drain kick), so calling it from here is
    // fine. Then go idle -> the scheduler loads Heavy, drains the task (a real
    // Heavy decode), persists the result, and restores Fast.
    const qint64 id = sched->enqueue(
        "summary",
        nlohmann::json{{"text", "Polymath is a local AI home assistant. "
                                "Summarize it in one sentence."}}, 5);
    assert(id > 0);

    // Flip idle on -> drain. Heavy (27B) partial-offload + a real decode is slow,
    // so allow a generous timeout. The default suite never runs this (opt-in).
    QMetaObject::invokeMethod(idle, [&] { emit idle->idleChanged(true); },
                              Qt::QueuedConnection);

    const bool got = waitFor([&] { return finished.load(); }, 600'000);
    assert(got && "deep task never finished (drain timed out)");
    assert(finishedId == id && "finished task id mismatch");

    // Verify the result persisted in the `tasks` table.
    std::string status, result;
    db.query("SELECT status,result_json FROM tasks WHERE id=?1", {id},
             [&](const Row& r) { status = r.text(0); result = r.text(1); });
    std::printf("[drain] task %lld status=%s result_chars=%zu\n",
                (long long)id, status.c_str(), result.size());
    assert(status == "done" && "deep task did not complete");
    assert(result.find("\"text\"") != std::string::npos && "no result text persisted");

    // taskFinished fires from inside drainQueue (scheduler thread) BEFORE that
    // method returns — the scheduler is still running requestHeavy(false) to
    // unload Heavy and restore Fast. Wait for that tail to complete so Fast is
    // resident again, the headline post-condition.
    assert(waitFor([&] { return !inf->heavyLoaded(); }, 120'000)
           && "Heavy still loaded after drain");

    // Orderly shutdown. Tear down the SCHEDULER thread first: it is the one that
    // calls cross-thread into the InferenceManager (requestHeavy/generate), so
    // the inference thread must outlive it. quit() returns to the event loop,
    // which has long finished drainQueue by now; wait() joins. Disconnect our
    // stack-capturing lambdas first so no late queued signal touches freed locals.
    QObject::disconnect(sched, nullptr, nullptr, nullptr);
    QObject::disconnect(idle,  nullptr, nullptr, nullptr);
    schedThread->quit(); schedThread->wait();
    infThread->quit();   infThread->wait();

    // The QObjects live on now-finished threads; deleting them cross-thread is
    // unsafe, and the process is exiting, so we intentionally leak them (and the
    // threads). All persisted state is already flushed to the DB.
    db.close();
    fs::remove(dbPath, ec);
    std::puts("[drain] OK — task drained on Heavy, Fast restored, no OOM");
    return true;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const fs::path modelsRoot = findModelsRoot();

    testBudgetMath();
    testGrammar(modelsRoot);
    testLifecycle(modelsRoot);
    testFullDrain(modelsRoot);

    std::puts("test_inference_e2e: OK");
    return 0;
}
