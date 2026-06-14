// =============================================================================
// test_instruments — unit tests for ReadInstrumentTool and RecordMeasurementTool.
//
//   A) read_instrument: ok=false when no reading exists; ok=true + correct fields
//      once a measurements row is present; latest reading is returned when multiple
//      rows exist.
//   B) record_measurement: in-range value => ok=true, in_range=true, row written
//      with source='voice'. Out-of-range value => ok=false, in_range=false.
//      No instrument row (ad-hoc) => always in_range (no range configured).
//      Missing value field => ok=false.
//
// Uses a temp DB; no LLM, no network.
// =============================================================================
#include "instrument_tool.h"

#include "database.h"
#include "config.h"
#include "paths.h"

#undef NDEBUG
#include <cassert>
#include <cmath>
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
//  A) read_instrument
// ---------------------------------------------------------------------------
void test_read_instrument(Database& db) {
    std::puts("[A] read_instrument");

    ReadInstrumentTool tool;
    ToolContext ctx; ctx.db = &db;

    // Missing instrument_id => ok=false.
    auto noId = tool.invoke({}, ctx);
    assert(!noId.ok);
    assert(noId.content.contains("error"));
    std::puts("  [ok] missing instrument_id => ok=false");

    // No readings yet => ok=false.
    auto noReading = tool.invoke({{"instrument_id", "scale-001"}}, ctx);
    assert(!noReading.ok);
    assert(noReading.content.value("error", "") == "no reading");
    std::puts("  [ok] no reading => ok=false, error='no reading'");

    // Seed an instrument + one measurement row.
    db.exec("INSERT OR IGNORE INTO instruments(id,device_id,name,channel,unit,device_class,"
            "expected_min,expected_max,created_at) "
            "VALUES('scale-001','dev-scale','Balance',0,'g','mass',0.0,500.0,1000000)", {});
    db.exec("INSERT INTO measurements(instrument_id,value,unit,in_range,source,ts) "
            "VALUES('scale-001',123.4,'g',1,'instrument',1700000100)", {});

    auto r1 = tool.invoke({{"instrument_id", "scale-001"}}, ctx);
    assert(r1.ok);
    assert(std::abs(r1.content["value"].get<double>() - 123.4) < 1e-9);
    assert(r1.content["unit"].get<std::string>() == "g");
    assert(r1.content["in_range"].get<bool>() == true);
    assert(r1.content["ts"].get<int64_t>() == 1700000100);
    std::puts("  [ok] reading returned with correct value/unit/in_range/ts");

    // Add a newer reading — must be the one returned (ORDER BY ts DESC).
    db.exec("INSERT INTO measurements(instrument_id,value,unit,in_range,source,ts) "
            "VALUES('scale-001',250.0,'g',1,'instrument',1700000200)", {});
    auto r2 = tool.invoke({{"instrument_id", "scale-001"}}, ctx);
    assert(r2.ok);
    assert(std::abs(r2.content["value"].get<double>() - 250.0) < 1e-9);
    std::puts("  [ok] latest measurement (highest ts) is returned");
}

// ---------------------------------------------------------------------------
//  B) record_measurement
// ---------------------------------------------------------------------------
void test_record_measurement(Database& db) {
    std::puts("[B] record_measurement");

    RecordMeasurementTool tool;
    ToolContext ctx; ctx.db = &db;

    // Ensure the instrument from part A is present (used for range checks).
    // scale-001 has expected range [0, 500].

    // Missing value field => ok=false.
    auto noVal = tool.invoke({{"kind", "mass"}, {"unit", "g"}}, ctx);
    assert(!noVal.ok);
    assert(noVal.content.contains("error"));
    std::puts("  [ok] missing value => ok=false");

    // In-range value: 200 g (within [0, 500]).
    auto inRange = tool.invoke({
        {"instrument_id", "scale-001"},
        {"kind",          "mass"},
        {"value",         200.0},
        {"unit",          "g"}
    }, ctx);
    assert(inRange.ok);
    assert(inRange.content["in_range"].get<bool>() == true);
    assert(inRange.content["recorded"].get<bool>() == true);
    const int64_t mid = inRange.content["measurement_id"].get<int64_t>();
    assert(mid > 0);
    // Verify row was written with source='voice'.
    assert(scalarCount(db, "SELECT COUNT(*) FROM measurements "
                           "WHERE id=?1 AND source='voice' AND in_range=1", {mid}) == 1);
    std::puts("  [ok] in-range value: ok=true, in_range=true, row source='voice'");

    // Out-of-range value: 600 g (above max 500).
    auto outRange = tool.invoke({
        {"instrument_id", "scale-001"},
        {"kind",          "mass"},
        {"value",         600.0},
        {"unit",          "g"}
    }, ctx);
    assert(!outRange.ok);  // ok=false so model re-asks
    assert(outRange.content["in_range"].get<bool>() == false);
    assert(outRange.content["recorded"].get<bool>() == true);
    const int64_t mid2 = outRange.content["measurement_id"].get<int64_t>();
    assert(scalarCount(db, "SELECT COUNT(*) FROM measurements "
                           "WHERE id=?1 AND source='voice' AND in_range=0", {mid2}) == 1);
    std::puts("  [ok] out-of-range value: ok=false, in_range=false, row written");

    // Ad-hoc (no instrument_id, no session): no range to violate => ok=true always.
    auto adHoc = tool.invoke({{"kind", "temperature"}, {"value", 9999.0}, {"unit", "°C"}}, ctx);
    assert(adHoc.ok);
    assert(adHoc.content["in_range"].get<bool>() == true);
    std::puts("  [ok] ad-hoc (no instrument) value always in_range");

    // Ad-hoc below-zero: also no range => in_range.
    auto adHocNeg = tool.invoke({{"kind", "voltage"}, {"value", -9999.0}, {"unit", "V"}}, ctx);
    assert(adHocNeg.ok);
    assert(adHocNeg.content["in_range"].get<bool>() == true);
    std::puts("  [ok] ad-hoc negative value always in_range");
}

} // anonymous namespace

// =============================================================================
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const auto root = fs::temp_directory_path() / "polymath_test_instruments";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    Paths::instance().setRoot(root);
    Paths::instance().ensureLayout();

    Database db;
    assert(db.open((root / "instruments.db").string()));
    Config(db).seedDefaults();

    test_read_instrument(db);
    test_record_measurement(db);

    db.close();
    fs::remove_all(root, ec);
    std::puts("test_instruments: OK");
    return 0;
}
