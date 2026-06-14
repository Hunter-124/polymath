#include "lab_session.h"
#include "documents.h"
#include "tool_support.h"
#include "database.h"
#include "event_bus.h"
#include "logging.h"

#include <QString>
#include <optional>

// Lab-session state machine. All state is persisted in `lab_sessions` and
// `lab_session_steps`; these tools only read/write those rows and emit a
// LabStepEvent so the UI/mobile reflect progress. finish_lab_session gathers the
// verified step values and hands them to GenerateLabReportTool to render a .docx.

namespace polymath {

namespace {

// A persisted step row (subset the tools care about).
struct Step {
    int64_t id = 0;
    int     step_no = 0;
    std::string prompt;
    std::string expected_kind;
    std::string expected_unit;
    std::optional<double> expected_min;
    std::optional<double> expected_max;
    bool   verified = false;
    std::optional<double> measured_value;
    std::string measured_unit;
};

bool sessionExists(Database& db, int64_t session_id, std::string* status = nullptr) {
    bool found = false;
    db.query("SELECT status FROM lab_sessions WHERE id=?1 LIMIT 1", {session_id},
             [&](const Row& r) { found = true; if (status) *status = r.text(0); });
    return found;
}

// First unverified step of a session ("the next step"), or nullopt if all done.
std::optional<Step> nextOpenStep(Database& db, int64_t session_id) {
    std::optional<Step> out;
    db.query("SELECT id,step_no,prompt,expected_kind,expected_unit,expected_min,expected_max,"
             "verified,measured_value,measured_unit FROM lab_session_steps "
             "WHERE session_id=?1 AND verified=0 ORDER BY step_no ASC LIMIT 1",
             {session_id}, [&](const Row& r) {
                 Step s;
                 s.id = r.i64(0); s.step_no = static_cast<int>(r.i64(1));
                 s.prompt = r.text(2); s.expected_kind = r.text(3); s.expected_unit = r.text(4);
                 if (!r.isNull(5)) s.expected_min = r.dbl(5);
                 if (!r.isNull(6)) s.expected_max = r.dbl(6);
                 s.verified = r.i64(7) != 0;
                 if (!r.isNull(8)) s.measured_value = r.dbl(8);
                 s.measured_unit = r.text(9);
                 out = s;
             });
    return out;
}

// A specific step by step_no, or (when step_no<=0) the next open step.
std::optional<Step> resolveStep(Database& db, int64_t session_id, int step_no) {
    if (step_no <= 0) return nextOpenStep(db, session_id);
    std::optional<Step> out;
    db.query("SELECT id,step_no,prompt,expected_kind,expected_unit,expected_min,expected_max,"
             "verified,measured_value,measured_unit FROM lab_session_steps "
             "WHERE session_id=?1 AND step_no=?2 LIMIT 1",
             {session_id, step_no}, [&](const Row& r) {
                 Step s;
                 s.id = r.i64(0); s.step_no = static_cast<int>(r.i64(1));
                 s.prompt = r.text(2); s.expected_kind = r.text(3); s.expected_unit = r.text(4);
                 if (!r.isNull(5)) s.expected_min = r.dbl(5);
                 if (!r.isNull(6)) s.expected_max = r.dbl(6);
                 s.verified = r.i64(7) != 0;
                 if (!r.isNull(8)) s.measured_value = r.dbl(8);
                 s.measured_unit = r.text(9);
                 out = s;
             });
    return out;
}

bool inRange(const Step& s, double v) {
    if (s.expected_min && v < *s.expected_min) return false;
    if (s.expected_max && v > *s.expected_max) return false;
    return true;
}

nlohmann::json optNum(const std::optional<double>& v) {
    return v ? nlohmann::json(*v) : nlohmann::json(nullptr);
}

// JSON view of a step's prompt + expected range, for tool results.
nlohmann::json stepJson(const Step& s) {
    return {
        {"step_no", s.step_no},
        {"prompt", s.prompt},
        {"expected_kind", s.expected_kind},
        {"expected_unit", s.expected_unit},
        {"expected_min", optNum(s.expected_min)},
        {"expected_max", optNum(s.expected_max)},
    };
}

void emitStep(int64_t session_id, const Step& s, const std::string& status,
              double measured = 0, const std::string& unit = "") {
    EventBus::instance().publishLabStep(
        {session_id, s.step_no, QString::fromStdString(s.prompt),
         QString::fromStdString(status), measured, QString::fromStdString(unit), s.verified});
}

} // namespace

// --- start_lab_session ------------------------------------------------------

std::string StartLabSessionTool::name() const { return "start_lab_session"; }
std::string StartLabSessionTool::description() const {
    return "Begin a guided lab session. Provide a title, an optional objective, and an optional "
           "ordered list of steps (each with a prompt, expected_kind/unit, and optional min/max "
           "range). Returns the session id and the first step to ask about.";
}

nlohmann::json StartLabSessionTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"title",     {{"type", "string"}, {"description", "Experiment / session title"}}},
            {"objective", {{"type", "string"}, {"description", "What the experiment sets out to determine"}}},
            {"steps", {
                {"type", "array"},
                {"description", "Ordered measurement steps"},
                {"items", {
                    {"type", "object"},
                    {"properties", {
                        {"prompt",        {{"type", "string"}, {"description", "What to ask/do at this step"}}},
                        {"expected_kind", {{"type", "string"}, {"description", "mass|temperature|time|ph|volume|..."}}},
                        {"expected_unit", {{"type", "string"}}},
                        {"expected_min",  {{"type", "number"}}},
                        {"expected_max",  {{"type", "number"}}},
                    }},
                    {"required", {"prompt"}},
                }},
            }},
        }},
        {"required", {"title"}},
    };
}

ToolResult StartLabSessionTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    const std::string title = args.value("title", "");
    if (title.empty())
        return {false, {{"error", "title required"}}, "start_lab_session: missing title"};

    const std::string objective = args.value("objective", "");
    const int64_t now = tool_support::nowUnix();
    const int64_t session_id = ctx.db->exec(
        "INSERT INTO lab_sessions(title,objective,status,started_at) VALUES(?1,?2,'active',?3)",
        {title, objective, now});

    // Insert any provided steps in order.
    int step_no = 0;
    if (args.contains("steps") && args["steps"].is_array()) {
        for (const auto& s : args["steps"]) {
            if (!s.is_object()) continue;
            ++step_no;
            nlohmann::json minP = (s.contains("expected_min") && s["expected_min"].is_number())
                                      ? s["expected_min"] : nlohmann::json(nullptr);
            nlohmann::json maxP = (s.contains("expected_max") && s["expected_max"].is_number())
                                      ? s["expected_max"] : nlohmann::json(nullptr);
            ctx.db->exec(
                "INSERT INTO lab_session_steps(session_id,step_no,prompt,expected_kind,"
                "expected_unit,expected_min,expected_max,verified) VALUES(?1,?2,?3,?4,?5,?6,?7,0)",
                {session_id, step_no, s.value("prompt", ""), s.value("expected_kind", ""),
                 s.value("expected_unit", ""), minP, maxP});
        }
    }

    PM_INFO("start_lab_session: id={} title='{}' steps={}", session_id, title, step_no);

    nlohmann::json content = {
        {"session_id", session_id},
        {"title", title},
        {"objective", objective},
        {"step_count", step_no},
    };
    auto first = nextOpenStep(*ctx.db, session_id);
    if (first) {
        emitStep(session_id, *first, "ask");
        content["first_step"] = stepJson(*first);
    } else {
        // No steps yet — the agent will add them dynamically via verify_lab_step.
        Step placeholder; placeholder.step_no = 0; placeholder.prompt = title;
        emitStep(session_id, placeholder, "started");
    }
    return {true, std::move(content), "Started lab session \"" + title + "\" (id " +
            std::to_string(session_id) + ")"};
}

// --- next_lab_step ----------------------------------------------------------

std::string NextLabStepTool::name() const { return "next_lab_step"; }
std::string NextLabStepTool::description() const {
    return "Get the next unverified step of a lab session: its prompt and expected range. Use this "
           "to know what to ask the user for next. Returns done=true when every step is verified.";
}

nlohmann::json NextLabStepTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"session_id", {{"type", "integer"}, {"description", "The lab session id"}}},
        }},
        {"required", {"session_id"}},
    };
}

ToolResult NextLabStepTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    const int64_t session_id = args.value("session_id", static_cast<int64_t>(0));
    if (!sessionExists(*ctx.db, session_id))
        return {false, {{"error", "unknown session"}}, "next_lab_step: unknown session_id"};

    auto step = nextOpenStep(*ctx.db, session_id);
    if (!step) {
        nlohmann::json content = {{"session_id", session_id}, {"done", true}};
        return {true, std::move(content), "All lab steps verified; ready to finish."};
    }

    emitStep(session_id, *step, "ask");
    nlohmann::json content = {{"session_id", session_id}, {"done", false}, {"step", stepJson(*step)}};
    PM_INFO("next_lab_step: session={} step_no={}", session_id, step->step_no);
    return {true, std::move(content), "Next step: " + step->prompt};
}

// --- verify_lab_step --------------------------------------------------------

std::string VerifyLabStepTool::name() const { return "verify_lab_step"; }
std::string VerifyLabStepTool::description() const {
    return "Record and verify a measured value against a lab step's expected range. If step_no is "
           "omitted the next open step is used. When the value is out of range the step is NOT "
           "marked verified and you should re-ask the user to re-measure.";
}

nlohmann::json VerifyLabStepTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"session_id", {{"type", "integer"}, {"description", "The lab session id"}}},
            {"step_no",    {{"type", "integer"}, {"description", "Step number; omit for the next open step"}}},
            {"value",      {{"type", "number"},  {"description", "The measured value the user reported"}}},
            {"unit",       {{"type", "string"},  {"description", "Unit of the value (optional)"}}},
        }},
        {"required", {"session_id", "value"}},
    };
}

ToolResult VerifyLabStepTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    const int64_t session_id = args.value("session_id", static_cast<int64_t>(0));
    if (!sessionExists(*ctx.db, session_id))
        return {false, {{"error", "unknown session"}}, "verify_lab_step: unknown session_id"};
    if (!args.contains("value") || !args["value"].is_number())
        return {false, {{"error", "value required"}}, "verify_lab_step: missing value"};

    const int step_no = args.value("step_no", 0);
    const double value = args["value"].get<double>();
    const std::string unit = args.value("unit", "");

    auto step = resolveStep(*ctx.db, session_id, step_no);
    if (!step)
        return {false, {{"error", "no such step"}, {"session_id", session_id}},
                "verify_lab_step: no open/matching step"};

    const bool ok = inRange(*step, value);
    const int64_t now = tool_support::nowUnix();

    // Record the measured value on the step regardless; mark verified only if in range.
    ctx.db->exec(
        "UPDATE lab_session_steps SET measured_value=?1,measured_unit=?2,verified=?3,verified_at=?4 "
        "WHERE id=?5",
        {value, unit, ok ? 1 : 0, ok ? nlohmann::json(now) : nlohmann::json(nullptr), step->id});

    // Also persist a measurements row tied to the session for the record.
    ctx.db->exec(
        "INSERT INTO measurements(instrument_id,session_id,value,unit,in_range,source,ts) "
        "VALUES(NULL,?1,?2,?3,?4,'voice',?5)",
        {session_id, value, unit, ok ? 1 : 0, now});

    step->verified = ok;
    step->measured_value = value;
    step->measured_unit = unit;
    emitStep(session_id, *step, ok ? "verified" : "out_of_range", value, unit);

    nlohmann::json content = {
        {"session_id", session_id},
        {"step_no", step->step_no},
        {"value", value}, {"unit", unit},
        {"verified", ok},
        {"in_range", ok},
        {"expected_min", optNum(step->expected_min)},
        {"expected_max", optNum(step->expected_max)},
    };
    PM_INFO("verify_lab_step: session={} step={} value={} verified={}",
            session_id, step->step_no, value, ok);
    if (!ok) {
        // ok=false => the model should re-ask for a fresh measurement.
        return {false, std::move(content),
                "Step " + std::to_string(step->step_no) + ": " + std::to_string(value) +
                " " + unit + " OUT OF RANGE — re-measure"};
    }
    return {true, std::move(content),
            "Step " + std::to_string(step->step_no) + " verified: " +
            std::to_string(value) + " " + unit};
}

// --- finish_lab_session -----------------------------------------------------

std::string FinishLabSessionTool::name() const { return "finish_lab_session"; }
std::string FinishLabSessionTool::description() const {
    return "Finish a lab session: mark it done, gather every verified step's value, and generate "
           "the lab report .docx from the collected data. Call this once all steps are verified.";
}

nlohmann::json FinishLabSessionTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"session_id", {{"type", "integer"}, {"description", "The lab session id"}}},
        }},
        {"required", {"session_id"}},
    };
}

ToolResult FinishLabSessionTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    const int64_t session_id = args.value("session_id", static_cast<int64_t>(0));
    std::string title, objective, status;
    if (!sessionExists(*ctx.db, session_id, &status))
        return {false, {{"error", "unknown session"}}, "finish_lab_session: unknown session_id"};

    ctx.db->query("SELECT title,objective FROM lab_sessions WHERE id=?1 LIMIT 1", {session_id},
                  [&](const Row& r) { title = r.text(0); objective = r.text(1); });

    // Gather verified steps in order; build a Results block + materials list.
    std::string results;
    nlohmann::json materials = nlohmann::json::array();
    int verified_count = 0;
    ctx.db->query(
        "SELECT step_no,prompt,expected_kind,measured_value,measured_unit FROM lab_session_steps "
        "WHERE session_id=?1 AND verified=1 ORDER BY step_no ASC",
        {session_id}, [&](const Row& r) {
            const int step_no = static_cast<int>(r.i64(0));
            const std::string prompt = r.text(1);
            const std::string kind   = r.text(2);
            const double value       = r.dbl(3);
            const std::string unit   = r.text(4);
            ++verified_count;
            const std::string label = kind.empty() ? prompt : kind;
            results += std::to_string(step_no) + ". " + label + ": " +
                       std::to_string(value) + " " + unit + "\n";
            if (!prompt.empty()) materials.push_back(prompt);
        });

    // Mark the session done now that we've collected its data.
    const int64_t now = tool_support::nowUnix();
    ctx.db->exec("UPDATE lab_sessions SET status='done',ended_at=?1 WHERE id=?2",
                 {now, session_id});

    Step doneMarker; doneMarker.step_no = verified_count; doneMarker.verified = true;
    doneMarker.prompt = title;
    EventBus::instance().publishLabStep(
        {session_id, verified_count, QString::fromStdString(title),
         QStringLiteral("done"), 0.0, QString(), true});

    // Build the lab-report args and hand them to the existing generator. The
    // heavy model expands the analysis/conclusion placeholders into prose; the
    // Results block carries the verified, in-range values verbatim.
    nlohmann::json reportArgs = {
        {"title", title.empty() ? ("Lab Session " + std::to_string(session_id)) : title},
        {"summary", objective},
        {"objective", objective},
        {"materials", materials},
        {"method", "Guided measurement session: each value below was captured by voice and "
                    "verified against its expected range before being recorded."},
        {"results", results.empty() ? "No verified measurements were recorded." : results},
        {"analysis", "[Discuss what the verified measurements indicate, comparing each against its "
                     "expected range and noting sources of error.]"},
        {"conclusion", "[State whether the objective was met based on the verified results.]"},
    };

    GenerateLabReportTool report;
    ToolResult gen = report.invoke(reportArgs, ctx);

    // Record the produced document id back on the session when known.
    int64_t doc_id = -1;
    if (gen.ok && gen.content.contains("document_id") && gen.content["document_id"].is_number()) {
        doc_id = gen.content["document_id"].get<int64_t>();
        ctx.db->exec("UPDATE lab_sessions SET report_doc_id=?1 WHERE id=?2", {doc_id, session_id});
    }

    PM_INFO("finish_lab_session: session={} verified={} report_doc_id={}",
            session_id, verified_count, doc_id);

    nlohmann::json content = {
        {"session_id", session_id},
        {"status", "done"},
        {"verified_steps", verified_count},
        {"report", gen.content},
    };
    if (doc_id >= 0) content["report_doc_id"] = doc_id;

    std::string summary = "Finished lab session \"" + title + "\" (" +
                          std::to_string(verified_count) + " verified) — " +
                          (gen.ok ? "report generated" : "report generation failed");
    return {gen.ok, std::move(content), summary};
}

} // namespace polymath
