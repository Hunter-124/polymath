// =============================================================================
// test_fabric — Wave 3 device-fabric unit tests.
//
// Tests the FabricService ingest* methods and DeviceRegistry CRUD:
//   A) ingestAnnounce: upserts edge_devices; camera kind also creates cameras row.
//      Re-announce updates without duplicating.
//   B) ingestReading: writes a measurements row with correct in_range vs
//      instruments.expected_min/max; asserts InstrumentReading signal fires.
//   C) ingestCameraEvent: writes events row with clip_url/confidence/device_id;
//      emits Detection. Resolves camera by device_id when cameraId<0.
//   D) DeviceRegistry: list/get/inRange edge cases (no range => in range).
//
// No MQTT, no network — all ingest* called directly with JSON payloads.
// Uses a temp DB and Paths setup mirrored from test_j_phase2_e2e.
// =============================================================================
#include "fabric_service.h"
#include "device_registry.h"

#include "database.h"
#include "config.h"
#include "paths.h"
#include "event_bus.h"
#include "types.h"

#include <QCoreApplication>
#include <QObject>

#undef NDEBUG   // keep assert() active even in Release
#include <cassert>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace polymath;
namespace fs = std::filesystem;

namespace {

// Count rows returned by a single-column COUNT(*) query.
int64_t scalarCount(Database& db, const std::string& sql,
                    const std::vector<nlohmann::json>& params = {}) {
    int64_t n = 0;
    db.query(sql, params, [&](const Row& r) { n = r.i64(0); });
    return n;
}

// ---------------------------------------------------------------------------
//  A) ingestAnnounce — upserts edge_devices; camera creates cameras row.
// ---------------------------------------------------------------------------
void test_announce(FabricService& svc, Database& db) {
    std::puts("[A] ingestAnnounce");

    // Announce a generic instrument device.
    nlohmann::json announce = {
        {"device_id", "test-inst-001"},
        {"kind",      "instrument"},
        {"name",      "HMM Balance"},
        {"location",  "lab"},
        {"transport", "mqtt"},
        {"endpoint",  "hearth/hmm/001"},
        {"fw",        "1.0.0"},
        {"instruments", nlohmann::json::array({
            {{"id", "hmm_001_balance_mass_g"},
             {"name", "Balance"},
             {"channel", 0},
             {"unit", "g"},
             {"device_class", "mass"},
             {"expected_min", 0.0},
             {"expected_max", 500.0}}
        })}
    };

    const std::string id = svc.ingestAnnounce(announce);
    assert(id == "test-inst-001");

    // edge_devices row must exist.
    assert(scalarCount(db, "SELECT COUNT(*) FROM edge_devices WHERE id='test-inst-001'") == 1);

    // instruments row must have been created by the announce.
    assert(scalarCount(db, "SELECT COUNT(*) FROM instruments WHERE id='hmm_001_balance_mass_g'") == 1);

    // Re-announce: must update, NOT duplicate.
    announce["fw"] = "1.1.0";
    const std::string id2 = svc.ingestAnnounce(announce);
    assert(id2 == "test-inst-001");
    assert(scalarCount(db, "SELECT COUNT(*) FROM edge_devices WHERE id='test-inst-001'") == 1);

    // fw_version should have been updated.
    std::string fw;
    db.query("SELECT fw_version FROM edge_devices WHERE id='test-inst-001'",
             {}, [&](const Row& r) { fw = r.text(0); });
    assert(fw == "1.1.0");
    std::puts("  [ok] instrument device upsert + no-duplicate on re-announce");

    // Announce a camera device — must also create a cameras row.
    nlohmann::json camAnnounce = {
        {"device_id", "test-cam-abc"},
        {"kind",      "camera"},
        {"name",      "Front Porch"},
        {"location",  "porch"},
        {"transport", "mjpeg"},
        {"endpoint",  "http://192.168.1.50"}
    };
    const std::string camId = svc.ingestAnnounce(camAnnounce);
    assert(camId == "test-cam-abc");
    assert(scalarCount(db, "SELECT COUNT(*) FROM edge_devices WHERE id='test-cam-abc' AND kind='camera'") == 1);
    assert(scalarCount(db, "SELECT COUNT(*) FROM cameras WHERE device_id='test-cam-abc'") == 1);

    // Re-announce camera: cameras row must not duplicate.
    svc.ingestAnnounce(camAnnounce);
    assert(scalarCount(db, "SELECT COUNT(*) FROM cameras WHERE device_id='test-cam-abc'") == 1);
    std::puts("  [ok] camera announce creates cameras row; re-announce does not duplicate");
}

// ---------------------------------------------------------------------------
//  B) ingestReading — measurements row + InstrumentReading signal.
// ---------------------------------------------------------------------------
void test_readings(FabricService& svc, Database& db) {
    std::puts("[B] ingestReading");

    // Instrument with range [0, 100].
    db.exec("INSERT OR IGNORE INTO instruments(id,device_id,name,channel,unit,device_class,"
            "expected_min,expected_max,created_at) "
            "VALUES('test-thermo','test-dev','Thermometer',0,'°C','temperature',0.0,100.0,"
            "1000000)", {});

    // Count signals using DirectConnection (same-thread direct delivery).
    std::atomic<int> readingCount{0};
    std::atomic<bool> lastInRange{true};
    std::atomic<double> lastValue{0.0};

    QObject sink;
    QObject::connect(&EventBus::instance(), &EventBus::instrumentReading, &sink,
                     [&](const InstrumentReading& r) {
                         if (r.instrument_id.toStdString() == "test-thermo") {
                             ++readingCount;
                             lastInRange = r.in_range;
                             lastValue   = r.value;
                         }
                     },
                     Qt::DirectConnection);

    // In-range reading: 37 °C (within [0,100]).
    nlohmann::json readingIn = {
        {"instrument_id", "test-thermo"},
        {"device_id",     "test-dev"},
        {"value",         37.0},
        {"unit",          "°C"},
        {"device_class",  "temperature"},
        {"ts",            1700000001}
    };
    svc.ingestReading(readingIn);
    assert(scalarCount(db, "SELECT COUNT(*) FROM measurements "
                           "WHERE instrument_id='test-thermo' AND in_range=1") == 1);
    assert(readingCount.load() == 1);
    assert(lastInRange.load() == true);
    std::puts("  [ok] in-range reading: measurements row written, InstrumentReading fired");

    // Out-of-range reading: 150 °C (above max 100).
    nlohmann::json readingOut = {
        {"instrument_id", "test-thermo"},
        {"device_id",     "test-dev"},
        {"value",         150.0},
        {"unit",          "°C"},
        {"device_class",  "temperature"},
        {"ts",            1700000002}
    };
    svc.ingestReading(readingOut);
    assert(scalarCount(db, "SELECT COUNT(*) FROM measurements "
                           "WHERE instrument_id='test-thermo' AND in_range=0") == 1);
    assert(readingCount.load() == 2);
    assert(lastInRange.load() == false);
    assert(lastValue.load() == 150.0);
    std::puts("  [ok] out-of-range reading: in_range=0, signal fired");

    QObject::disconnect(&sink, nullptr, nullptr, nullptr);
}

// ---------------------------------------------------------------------------
//  C) ingestCameraEvent — events row + Detection; resolve camera by device_id.
// ---------------------------------------------------------------------------
void test_camera_event(FabricService& svc, Database& db) {
    std::puts("[C] ingestCameraEvent");

    // Seed the camera device + cameras row for device_id resolution.
    db.exec("INSERT OR IGNORE INTO edge_devices(id,kind,name,location,transport,endpoint,"
            "capabilities,fw_version,last_seen,enabled,created_at) "
            "VALUES('cam-dev-01','camera','Side Cam','side','mjpeg','http://10.0.0.5',"
            "'{}','',1700000000,1,1700000000)", {});
    const int64_t camRowId = db.exec(
        "INSERT OR IGNORE INTO cameras(name,url,location,enabled,device_id) "
        "VALUES('Side Cam','http://10.0.0.5/stream','side',1,'cam-dev-01')");

    std::atomic<int> detectionCount{0};
    std::atomic<double> lastConf{0.0};
    QObject sink;
    QObject::connect(&EventBus::instance(), &EventBus::detection, &sink,
                     [&](const Detection& d) {
                         if (d.camera_id == static_cast<int>(camRowId)) {
                             ++detectionCount;
                             if (!d.boxes.empty()) lastConf = d.boxes[0].score;
                         }
                     },
                     Qt::DirectConnection);

    // Direct camera-id call with clip_url + confidence.
    nlohmann::json ev = {
        {"kind",        "motion"},
        {"device_id",   "cam-dev-01"},
        {"clip_url",    "https://cdn.example.com/clip42.mp4"},
        {"confidence",  0.87},
        {"ts",          1700001000}
    };
    const int64_t evId = svc.ingestCameraEvent(camRowId, ev);
    assert(evId > 0);
    // Verify events row has the new v2 columns.
    int rowsOk = 0;
    db.query("SELECT COUNT(*) FROM events WHERE id=?1 AND clip_url='https://cdn.example.com/clip42.mp4' "
             "AND confidence > 0.8 AND device_id='cam-dev-01'",
             {evId}, [&](const Row& r) { rowsOk = static_cast<int>(r.i64(0)); });
    assert(rowsOk == 1);
    assert(detectionCount.load() == 1);
    assert(lastConf.load() > 0.8);
    std::puts("  [ok] explicit cameraId: events row with clip_url/confidence/device_id + Detection");

    // Resolve camera by device_id when cameraId < 0.
    nlohmann::json ev2 = {
        {"kind",        "person"},
        {"device_id",   "cam-dev-01"},
        {"clip_url",    "https://cdn.example.com/clip43.mp4"},
        {"confidence",  0.95},
        {"ts",          1700001010}
    };
    const int64_t evId2 = svc.ingestCameraEvent(-1, ev2);
    assert(evId2 > 0);
    int rowsOk2 = 0;
    db.query("SELECT COUNT(*) FROM events WHERE id=?1 AND kind='person' AND device_id='cam-dev-01'",
             {evId2}, [&](const Row& r) { rowsOk2 = static_cast<int>(r.i64(0)); });
    assert(rowsOk2 == 1);
    assert(detectionCount.load() == 2);
    std::puts("  [ok] cameraId<0 resolves camera via device_id; event row written + Detection fired");

    QObject::disconnect(&sink, nullptr, nullptr, nullptr);
}

// ---------------------------------------------------------------------------
//  D) DeviceRegistry — list/get/inRange edge cases.
// ---------------------------------------------------------------------------
void test_registry(DeviceRegistry& reg, Database& db) {
    std::puts("[D] DeviceRegistry list/get/inRange");

    // Seeds from previous tests are already present; add a deterministic one.
    db.exec("INSERT OR IGNORE INTO edge_devices(id,kind,name,location,transport,endpoint,"
            "capabilities,fw_version,last_seen,enabled,created_at) "
            "VALUES('panel-xyz','panel','Kitchen Panel','kitchen','http','http://10.0.0.9',"
            "'{}','2.0',1700000000,1,1700000000)", {});

    // list() — must include the device we just inserted.
    const auto all = reg.list();
    bool found = false;
    for (const auto& d : all) if (d.id == "panel-xyz") { found = true; break; }
    assert(found);
    std::puts("  [ok] list() returns inserted device");

    // list(kind) — filter.
    const auto panels = reg.list("panel");
    for (const auto& d : panels) assert(d.kind == "panel");
    std::puts("  [ok] list(kind) returns only devices of that kind");

    // get() — existing id.
    auto opt = reg.get("panel-xyz");
    assert(opt.has_value());
    assert(opt->name == "Kitchen Panel");
    assert(opt->kind == "panel");
    std::puts("  [ok] get() returns existing device");

    // get() — unknown id returns nullopt.
    auto missing = reg.get("does-not-exist");
    assert(!missing.has_value());
    std::puts("  [ok] get() returns nullopt for unknown id");

    // inRange — instrument with min/max: in-range value.
    db.exec("INSERT OR IGNORE INTO instruments(id,device_id,name,channel,unit,device_class,"
            "expected_min,expected_max,created_at) "
            "VALUES('ph-sensor-01','panel-xyz','pH Probe',0,'pH','ph',6.0,8.0,1700000000)", {});
    assert(reg.inRange("ph-sensor-01", 7.0) == true);
    assert(reg.inRange("ph-sensor-01", 5.9) == false);
    assert(reg.inRange("ph-sensor-01", 8.1) == false);
    std::puts("  [ok] inRange returns correct result for bounded instrument");

    // inRange — no instrument row (unknown id) => true (no constraint to violate).
    assert(reg.inRange("nonexistent-instr", 9999.0) == true);
    std::puts("  [ok] inRange returns true for unknown instrument (no range configured)");

    // inRange — instrument with NULL expected_min/max (no bounds row).
    db.exec("INSERT OR IGNORE INTO instruments(id,device_id,name,channel,unit,device_class,"
            "expected_min,expected_max,created_at) "
            "VALUES('open-ended-01','panel-xyz','Open Sensor',0,'V','voltage',NULL,NULL,1700000000)",
            {});
    assert(reg.inRange("open-ended-01", -9999.0) == true);
    assert(reg.inRange("open-ended-01",  9999.0) == true);
    std::puts("  [ok] inRange returns true when both bounds are NULL");
}

} // anonymous namespace

// =============================================================================
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QCoreApplication app(argc, argv);

    // Temp root mirroring the pattern in test_j_phase2_e2e.
    const auto root = fs::temp_directory_path() / "polymath_test_fabric";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    Paths::instance().setRoot(root);
    Paths::instance().ensureLayout();

    Database db;
    assert(db.open((root / "fabric.db").string()));
    Config(db).seedDefaults();

    FabricService svc(db);

    test_announce(svc, db);
    test_readings(svc, db);
    test_camera_event(svc, db);
    test_registry(svc.registry(), db);

    db.close();
    fs::remove_all(root, ec);
    std::puts("test_fabric: OK");
    return 0;
}
