#include "instrument_tool.h"
#include "tool_support.h"
#include "database.h"
#include "event_bus.h"
#include "logging.h"

#include <QString>
#include <optional>

// read_instrument / record_measurement — the voice-side bridge to the lab
// instrument fabric. The fabric writes instrument-pushed readings into
// `measurements` (source='instrument') already; these tools let the agent read
// the latest value back and record voice/manual values with a range check.
//
// Range check: a value is in_range when it falls within [expected_min,
// expected_max]. The bound source is, in order: the active lab step's expected
// range (when a session_id is given), else the instrument's expected_min/max.
// A null bound on either side means "unbounded on that side".

namespace polymath {

namespace {

// A resolved expected range (either bound may be absent => unbounded that side).
struct Range {
    std::optional<double> min;
    std::optional<double> max;
    bool bounded() const { return min || max; }
    bool contains(double v) const {
        if (min && v < *min) return false;
        if (max && v > *max) return false;
        return true;
    }
};

// Expected range for an instrument (from instruments.expected_min/max).
Range instrumentRange(Database& db, const std::string& instrument_id) {
    Range r;
    if (instrument_id.empty()) return r;
    db.query("SELECT expected_min,expected_max FROM instruments WHERE id=?1 LIMIT 1",
             {instrument_id}, [&](const Row& row) {
                 if (!row.isNull(0)) r.min = row.dbl(0);
                 if (!row.isNull(1)) r.max = row.dbl(1);
             });
    return r;
}

// Expected range carried by the next unverified step of a session (the step the
// agent is currently asking about). Empty when the session has no open step.
Range sessionStepRange(Database& db, int64_t session_id) {
    Range r;
    if (session_id <= 0) return r;
    db.query("SELECT expected_min,expected_max FROM lab_session_steps "
             "WHERE session_id=?1 AND verified=0 ORDER BY step_no ASC LIMIT 1",
             {session_id}, [&](const Row& row) {
                 if (!row.isNull(0)) r.min = row.dbl(0);
                 if (!row.isNull(1)) r.max = row.dbl(1);
             });
    return r;
}

nlohmann::json optNum(const std::optional<double>& v) {
    return v ? nlohmann::json(*v) : nlohmann::json(nullptr);
}

} // namespace

// --- read_instrument --------------------------------------------------------

std::string ReadInstrumentTool::name() const { return "read_instrument"; }
std::string ReadInstrumentTool::description() const {
    return "Read the latest reading from a lab instrument by its id. Returns the most recent "
           "value, unit, whether it was in its expected range, and the timestamp.";
}

nlohmann::json ReadInstrumentTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"instrument_id", {{"type", "string"},
                               {"description", "Instrument id, e.g. hmm_a1b2_balance_mass_g"}}},
            {"channel",       {{"type", "integer"},
                               {"description", "Optional channel filter (unused by most single-channel instruments)"}}},
        }},
        {"required", {"instrument_id"}},
    };
}

ToolResult ReadInstrumentTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    // Be defensive: args may arrive as null / a non-object from a stray tool call.
    const std::string instrument_id =
        args.is_object() ? args.value("instrument_id", std::string()) : std::string();
    if (instrument_id.empty())
        return {false, {{"error", "instrument_id required"}}, "read_instrument: missing instrument_id"};

    bool found = false;
    double value = 0; std::string unit; bool in_range = true; int64_t ts = 0;
    ctx.db->query(
        "SELECT value,unit,in_range,ts FROM measurements WHERE instrument_id=?1 "
        "ORDER BY ts DESC, id DESC LIMIT 1",
        {instrument_id}, [&](const Row& r) {
            value    = r.dbl(0);
            unit     = r.text(1);
            in_range = r.i64(2) != 0;
            ts       = r.i64(3);
            found = true;
        });

    if (!found) {
        return {false, {{"error", "no reading"}, {"instrument_id", instrument_id}},
                "read_instrument: no reading for " + instrument_id};
    }

    nlohmann::json content = {
        {"instrument_id", instrument_id},
        {"value", value}, {"unit", unit}, {"in_range", in_range}, {"ts", ts},
    };
    PM_INFO("read_instrument: {} = {} {} (in_range={})", instrument_id, value, unit, in_range);
    return {true, std::move(content),
            "Read " + instrument_id + ": " + std::to_string(value) + " " + unit};
}

// --- record_measurement -----------------------------------------------------

std::string RecordMeasurementTool::name() const { return "record_measurement"; }
std::string RecordMeasurementTool::description() const {
    return "Record a measured value spoken or entered by the user. Computes whether the value is "
           "within the expected range (instrument range, or the active lab step's range when a "
           "session_id is given). Reports back out-of-range so you can re-ask for the measurement.";
}

nlohmann::json RecordMeasurementTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"instrument_id", {{"type", "string"},
                               {"description", "Optional instrument id this value came from"}}},
            {"kind",          {{"type", "string"},
                               {"description", "What was measured: mass|temperature|time|ph|volume|..."}}},
            {"value",         {{"type", "number"}, {"description", "The measured value"}}},
            {"unit",          {{"type", "string"}, {"description", "Unit, e.g. g, °C, s, pH"}}},
            {"session_id",    {{"type", "integer"},
                               {"description", "Optional lab session id (uses its open step's range)"}}},
        }},
        {"required", {"kind", "value", "unit"}},
    };
}

ToolResult RecordMeasurementTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    if (!args.is_object() || !args.contains("value") || !args["value"].is_number())
        return {false, {{"error", "value required"}}, "record_measurement: missing value"};

    const std::string instrument_id = args.value("instrument_id", "");
    const std::string kind          = args.value("kind", "");
    const std::string unit          = args.value("unit", "");
    const double      value         = args["value"].get<double>();
    const int64_t     session_id    = args.value("session_id", static_cast<int64_t>(0));

    // Prefer the active step's range when in a session; else the instrument's.
    Range range = sessionStepRange(*ctx.db, session_id);
    if (!range.bounded()) range = instrumentRange(*ctx.db, instrument_id);
    const bool in_range = !range.bounded() || range.contains(value);

    nlohmann::json instrParam = instrument_id.empty() ? nlohmann::json(nullptr)
                                                       : nlohmann::json(instrument_id);
    nlohmann::json sessParam  = session_id > 0 ? nlohmann::json(session_id)
                                               : nlohmann::json(nullptr);
    const int64_t now = tool_support::nowUnix();
    const int64_t id = ctx.db->exec(
        "INSERT INTO measurements(instrument_id,session_id,value,unit,in_range,source,ts) "
        "VALUES(?1,?2,?3,?4,?5,'voice',?6)",
        {instrParam, sessParam, value, unit, in_range ? 1 : 0, now});

    PM_INFO("record_measurement: id={} kind={} value={} {} in_range={}",
            id, kind, value, unit, in_range);
    nlohmann::json content = {
        {"recorded", true},
        {"measurement_id", id},
        {"kind", kind}, {"value", value}, {"unit", unit},
        {"in_range", in_range},
        {"expected_min", optNum(range.min)},
        {"expected_max", optNum(range.max)},
    };
    // ok=false when out of range so the model re-asks for the measurement.
    if (!in_range) {
        return {false, std::move(content),
                "Recorded " + kind + " = " + std::to_string(value) + " " + unit + " (OUT OF RANGE)"};
    }
    return {true, std::move(content),
            "Recorded " + kind + " = " + std::to_string(value) + " " + unit};
}

} // namespace polymath
