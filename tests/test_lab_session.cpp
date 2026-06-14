// =============================================================================
// test_lab_session — unit tests for the lab-session state machine.
//
// Drives the complete lifecycle:
//   start_lab_session -> next_lab_step -> verify_lab_step (in-range AND
//   out-of-range) -> finish_lab_session
//
// Specific assertions:
//   A) start_lab_session: lab_sessions row created (status='active'); step rows
//      inserted; first_step returned in content; LabStepEvent fires.
//   B) next_lab_step: returns the next unverified step; returns done=true when
//      all steps are verified; returns ok=false for unknown session_id.
//   C) verify_lab_step (in-range): lab_session_steps.verified=1 and
//      measurements row written; ok=true; LabStepEvent fires status='verified'.
//   D) verify_lab_step (out-of-range): ok=false; step stays unverified (verified=0);
//      LabStepEvent fires status='out_of_range'; re-verify with in-range value
//      transitions to verified=1.
//   E) finish_lab_session: lab_sessions.status='done'; report_doc_id set (or
//      report key present in content); LabStepEvent fires status='done'.
//      Calling finish on an unknown session_id => ok=false.
//
// Uses QCoreApplication for DirectConnection signals (same thread), mirrored
// from test_j_phase2_e2e. No LLM, no network.
// =============================================================================
#include "lab_session.h"
#include "instrument_tool.h"

#include "database.h"
#include "config.h"
#include "paths.h"
#include "event_bus.h"
#include "types.h"

#include <QCoreApplication>
#include <QObject>

#undef NDEBUG
#include <cassert>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace polymath;
namespace fs = std::filesystem;

namespace {

int64_t scalarCount(Database& db, const std::string& sql,
                    const std::vector<nlohmann::json>& params = {}) {
    int64_t n = 0;
    db.query(sql, params, [&](const Row& r) { n = r.i64(0); });
    return n;
}

// ---------------------------------------------------------------------------
//  Convenience: all four tool instances live here to keep main() clean.
// ---------------------------------------------------------------------------
struct LabTools {
    StartLabSessionTool  start;
    NextLabStepTool      next;
    VerifyLabStepTool    verify;
    FinishLabSessionTool finish;
};

// ---------------------------------------------------------------------------
//  A) start_lab_session
// ---------------------------------------------------------------------------
int64_t test_start(LabTools& t, ToolContext& ctx) {
    std::puts("[A] start_lab_session");

    // Missing title => ok=false.
    auto bad = t.start.invoke({{"objective", "should fail"}}, ctx);
    assert(!bad.ok);
    std::puts("  [ok] missing title => ok=false");

    // Collect LabStepEvents.
    std::atomic<int> labStepFired{0};
    std::atomic<int> lastStepNo{-1};
    std::string lastStatus;
    QObject sink;
    QObject::connect(&EventBus::instance(), &EventBus::labStep, &sink,
                     [&](const LabStepEvent& ev) {
                         ++labStepFired;
                         lastStepNo = ev.step_no;
                         lastStatus = ev.status.toStdString();
                     },
                     Qt::DirectConnection);

    // Valid start with 2 steps.
    nlohmann::json args = {
        {"title",     "Caffeine Extraction"},
        {"objective", "Isolate caffeine from tea leaves."},
        {"steps", nlohmann::json::array({
            {{"prompt", "Measure initial sample mass."},
             {"expected_kind", "mass"}, {"expected_unit", "g"},
             {"expected_min", 1.0},    {"expected_max", 5.0}},
            {{"prompt", "Record extraction temperature."},
             {"expected_kind", "temperature"}, {"expected_unit", "°C"},
             {"expected_min", 60.0},           {"expected_max", 80.0}}
        })}
    };

    auto r = t.start.invoke(args, ctx);
    assert(r.ok);
    const int64_t session_id = r.content["session_id"].get<int64_t>();
    assert(session_id > 0);

    // DB state.
    assert(scalarCount(*ctx.db, "SELECT COUNT(*) FROM lab_sessions "
                                "WHERE id=?1 AND status='active'", {session_id}) == 1);
    assert(scalarCount(*ctx.db, "SELECT COUNT(*) FROM lab_session_steps "
                                "WHERE session_id=?1", {session_id}) == 2);

    // first_step present in content.
    assert(r.content.contains("first_step"));
    assert(r.content["first_step"]["step_no"].get<int>() == 1);
    assert(r.content["step_count"].get<int>() == 2);

    // LabStepEvent fired for the first step.
    assert(labStepFired.load() >= 1);
    assert(lastStepNo.load() == 1);
    assert(lastStatus == "ask");
    std::puts("  [ok] session created, steps inserted, first_step returned, LabStepEvent fired");

    QObject::disconnect(&sink, nullptr, nullptr, nullptr);
    return session_id;
}

// ---------------------------------------------------------------------------
//  B) next_lab_step
// ---------------------------------------------------------------------------
void test_next(LabTools& t, ToolContext& ctx, int64_t session_id) {
    std::puts("[B] next_lab_step");

    // Unknown session => ok=false.
    auto bad = t.next.invoke({{"session_id", static_cast<int64_t>(99999)}}, ctx);
    assert(!bad.ok);
    std::puts("  [ok] unknown session_id => ok=false");

    // Step 1 is not yet verified — must return done=false with step_no=1.
    std::atomic<int> stepFired{0};
    QObject sink;
    QObject::connect(&EventBus::instance(), &EventBus::labStep, &sink,
                     [&](const LabStepEvent&) { ++stepFired; },
                     Qt::DirectConnection);

    auto r = t.next.invoke({{"session_id", session_id}}, ctx);
    assert(r.ok);
    assert(r.content["done"].get<bool>() == false);
    assert(r.content["step"]["step_no"].get<int>() == 1);
    assert(stepFired.load() >= 1);
    std::puts("  [ok] next returns first unverified step, done=false, LabStepEvent fired");

    QObject::disconnect(&sink, nullptr, nullptr, nullptr);
}

// ---------------------------------------------------------------------------
//  C) verify_lab_step — in-range
// ---------------------------------------------------------------------------
void test_verify_in_range(LabTools& t, ToolContext& ctx, int64_t session_id) {
    std::puts("[C] verify_lab_step (in-range)");

    std::atomic<int> stepFired{0};
    std::string lastStatus;
    QObject sink;
    QObject::connect(&EventBus::instance(), &EventBus::labStep, &sink,
                     [&](const LabStepEvent& ev) {
                         ++stepFired;
                         lastStatus = ev.status.toStdString();
                     },
                     Qt::DirectConnection);

    // 2.5 g is within [1.0, 5.0].
    auto r = t.verify.invoke({
        {"session_id", session_id},
        {"step_no",    1},
        {"value",      2.5},
        {"unit",       "g"}
    }, ctx);
    assert(r.ok);
    assert(r.content["verified"].get<bool>() == true);
    assert(r.content["in_range"].get<bool>() == true);
    assert(r.content["step_no"].get<int>() == 1);

    // DB: step should be verified=1.
    assert(scalarCount(*ctx.db, "SELECT COUNT(*) FROM lab_session_steps "
                                "WHERE session_id=?1 AND step_no=1 AND verified=1",
                       {session_id}) == 1);
    // A measurements row tied to the session should have been written.
    assert(scalarCount(*ctx.db, "SELECT COUNT(*) FROM measurements "
                                "WHERE session_id=?1 AND in_range=1",
                       {session_id}) >= 1);

    assert(stepFired.load() >= 1);
    assert(lastStatus == "verified");
    std::puts("  [ok] in-range verify: step marked verified=1, measurements row written, signal fired");

    QObject::disconnect(&sink, nullptr, nullptr, nullptr);
}

// ---------------------------------------------------------------------------
//  D) verify_lab_step — out-of-range then retry
// ---------------------------------------------------------------------------
void test_verify_out_of_range(LabTools& t, ToolContext& ctx, int64_t session_id) {
    std::puts("[D] verify_lab_step (out-of-range then retry)");

    std::atomic<int> stepFired{0};
    std::string lastStatus;
    QObject sink;
    QObject::connect(&EventBus::instance(), &EventBus::labStep, &sink,
                     [&](const LabStepEvent& ev) {
                         ++stepFired;
                         lastStatus = ev.status.toStdString();
                     },
                     Qt::DirectConnection);

    // Step 2 is temperature [60, 80]. 95 °C is out of range.
    auto bad = t.verify.invoke({
        {"session_id", session_id},
        {"step_no",    2},
        {"value",      95.0},
        {"unit",       "°C"}
    }, ctx);
    assert(!bad.ok);  // ok=false: model should re-ask
    assert(bad.content["verified"].get<bool>() == false);
    assert(bad.content["in_range"].get<bool>() == false);

    // Step must still be unverified.
    assert(scalarCount(*ctx.db, "SELECT COUNT(*) FROM lab_session_steps "
                                "WHERE session_id=?1 AND step_no=2 AND verified=0",
                       {session_id}) == 1);
    assert(stepFired.load() >= 1);
    assert(lastStatus == "out_of_range");
    std::puts("  [ok] out-of-range: ok=false, step stays verified=0, signal 'out_of_range'");

    // Re-verify with a good value: 72 °C.
    auto good = t.verify.invoke({
        {"session_id", session_id},
        {"step_no",    2},
        {"value",      72.0},
        {"unit",       "°C"}
    }, ctx);
    assert(good.ok);
    assert(good.content["verified"].get<bool>() == true);
    assert(scalarCount(*ctx.db, "SELECT COUNT(*) FROM lab_session_steps "
                                "WHERE session_id=?1 AND step_no=2 AND verified=1",
                       {session_id}) == 1);
    assert(lastStatus == "verified");
    std::puts("  [ok] re-verify with in-range value: step transitions to verified=1");

    QObject::disconnect(&sink, nullptr, nullptr, nullptr);
}

// ---------------------------------------------------------------------------
//  E) finish_lab_session
// ---------------------------------------------------------------------------
void test_finish(LabTools& t, ToolContext& ctx, int64_t session_id) {
    std::puts("[E] finish_lab_session");

    // Unknown session => ok=false.
    auto bad = t.finish.invoke({{"session_id", static_cast<int64_t>(99999)}}, ctx);
    assert(!bad.ok);
    std::puts("  [ok] unknown session_id => ok=false");

    // All steps are verified; finish.
    std::atomic<int> doneFired{0};
    QObject sink;
    QObject::connect(&EventBus::instance(), &EventBus::labStep, &sink,
                     [&](const LabStepEvent& ev) {
                         if (ev.status == QStringLiteral("done")) ++doneFired;
                     },
                     Qt::DirectConnection);

    auto r = t.finish.invoke({{"session_id", session_id}}, ctx);
    // finish_lab_session calls GenerateLabReportTool which writes a .docx + documents row.
    // ok may be false if docx writing fails in CI (no Qt Gui render), but the session
    // transitions to 'done' regardless. Check the DB rows directly.
    (void)r;  // ok depends on GenerateLabReportTool; don't assert it here

    // Session must be marked done.
    assert(scalarCount(*ctx.db, "SELECT COUNT(*) FROM lab_sessions "
                                "WHERE id=?1 AND status='done'", {session_id}) == 1);
    // ended_at must be set.
    int64_t endedAt = 0;
    ctx.db->query("SELECT ended_at FROM lab_sessions WHERE id=?1", {session_id},
                  [&](const Row& rr) { endedAt = rr.i64(0); });
    assert(endedAt > 0);

    // LabStepEvent with status='done' must have fired.
    assert(doneFired.load() >= 1);
    std::puts("  [ok] session status='done', ended_at set, LabStepEvent 'done' fired");

    // The content must include 'report' key (GenerateLabReportTool result, ok or not).
    assert(r.content.contains("report"));
    assert(r.content["session_id"].get<int64_t>() == session_id);
    std::puts("  [ok] content has session_id and report key");

    // report_doc_id should be set if generation succeeded.
    if (r.ok && r.content.contains("report_doc_id") && !r.content["report_doc_id"].is_null()) {
        const int64_t docId = r.content["report_doc_id"].get<int64_t>();
        assert(docId > 0);
        assert(scalarCount(*ctx.db, "SELECT COUNT(*) FROM lab_sessions "
                                    "WHERE id=?1 AND report_doc_id=?2",
                           {session_id, docId}) == 1);
        std::puts("  [ok] report_doc_id back-linked on lab_sessions row");
    } else {
        std::puts("  [note] report generation skipped (no docx writer in CI) — DB row verified");
    }

    QObject::disconnect(&sink, nullptr, nullptr, nullptr);
}

// ---------------------------------------------------------------------------
//  F) next_lab_step after all steps verified => done=true
// ---------------------------------------------------------------------------
void test_next_all_done(LabTools& t, ToolContext& ctx, int64_t session_id) {
    std::puts("[F] next_lab_step after all steps verified");

    auto r = t.next.invoke({{"session_id", session_id}}, ctx);
    assert(r.ok);
    assert(r.content["done"].get<bool>() == true);
    std::puts("  [ok] next_lab_step returns done=true when all steps verified");
}

} // anonymous namespace

// =============================================================================
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QCoreApplication app(argc, argv);

    const auto root = fs::temp_directory_path() / "polymath_test_lab_session";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    Paths::instance().setRoot(root);
    Paths::instance().ensureLayout();

    Database db;
    assert(db.open((root / "lab.db").string()));
    Config(db).seedDefaults();

    ToolContext ctx; ctx.db = &db;
    LabTools t;

    const int64_t session_id = test_start(t, ctx);
    test_next(t, ctx, session_id);
    test_verify_in_range(t, ctx, session_id);
    test_verify_out_of_range(t, ctx, session_id);
    test_finish(t, ctx, session_id);
    test_next_all_done(t, ctx, session_id);

    db.close();
    fs::remove_all(root, ec);
    std::puts("test_lab_session: OK");
    return 0;
}
