#include "device_registry.h"

#include "database.h"
#include "logging.h"

#include <chrono>

namespace polymath {

using nlohmann::json;

namespace {
int64_t nowUnix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
// Best-effort string field read from a JSON object.
std::string str(const json& j, const char* k, const std::string& def = "") {
    if (auto it = j.find(k); it != j.end() && it->is_string()) return it->get<std::string>();
    return def;
}
} // namespace

std::string DeviceRegistry::upsertFromAnnounce(const json& a) {
    const std::string id = str(a, "device_id");
    if (id.empty()) { PM_WARN("fabric: announce without device_id"); return {}; }

    const std::string kind     = str(a, "kind", "camera");
    const std::string name     = str(a, "name", id);
    const std::string location = str(a, "location");
    const std::string endpoint = str(a, "endpoint");
    const std::string transport = str(a, "transport", "mqtt");
    const std::string fw       = str(a, "fw");
    const std::string caps     = a.contains("capabilities") ? a["capabilities"].dump() : "{}";
    const int64_t now = nowUnix();

    // Upsert edge_devices (keep created_at on first insert, refresh the rest).
    db_.exec(
        "INSERT INTO edge_devices(id,kind,name,location,transport,endpoint,"
        "capabilities,fw_version,last_seen,enabled,created_at) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,1,?9) "
        "ON CONFLICT(id) DO UPDATE SET kind=excluded.kind, name=excluded.name, "
        "location=excluded.location, transport=excluded.transport, "
        "endpoint=excluded.endpoint, capabilities=excluded.capabilities, "
        "fw_version=excluded.fw_version, last_seen=excluded.last_seen",
        {id, kind, name, location, transport, endpoint, caps, fw, now});

    if (kind == "camera" && !endpoint.empty())
        cameraIdForDevice(id, name, endpoint + "/stream");

    if (kind == "instrument" && a.contains("instruments") && a["instruments"].is_array())
        for (const auto& inst : a["instruments"]) upsertInstrument(inst, id);

    PM_INFO("fabric: registered device {} ({}, {})", id, kind, name);
    return id;
}

void DeviceRegistry::setPresence(const std::string& deviceId, bool online, int64_t ts) {
    if (ts <= 0) ts = nowUnix();
    if (online)
        db_.exec("UPDATE edge_devices SET last_seen=?1 WHERE id=?2", {ts, deviceId});
    // Offline is derived from last_seen age at read time; nothing to write here
    // beyond not refreshing last_seen.
}

void DeviceRegistry::touch(const std::string& deviceId, int64_t ts) {
    if (ts <= 0) ts = nowUnix();
    db_.exec("UPDATE edge_devices SET last_seen=?1 WHERE id=?2", {ts, deviceId});
}

void DeviceRegistry::upsertInstrument(const json& inst, const std::string& deviceId) {
    const std::string id = str(inst, "id");
    if (id.empty()) return;
    const std::string name = str(inst, "name", id);
    const int channel = inst.value("channel", 0);
    const std::string unit = str(inst, "unit");
    const std::string dclass = str(inst, "device_class");
    const int64_t now = nowUnix();

    // expected_min/max are optional; bind NULL when absent so "no range" is honest.
    std::vector<nlohmann::json> params = {id, deviceId, name, channel, unit, dclass};
    const bool hasMin = inst.contains("expected_min") && inst["expected_min"].is_number();
    const bool hasMax = inst.contains("expected_max") && inst["expected_max"].is_number();
    params.push_back(hasMin ? json(inst["expected_min"].get<double>()) : json(nullptr));
    params.push_back(hasMax ? json(inst["expected_max"].get<double>()) : json(nullptr));
    params.push_back(now);

    db_.exec(
        "INSERT INTO instruments(id,device_id,name,channel,unit,device_class,"
        "expected_min,expected_max,created_at) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9) "
        "ON CONFLICT(id) DO UPDATE SET device_id=excluded.device_id, "
        "name=excluded.name, channel=excluded.channel, unit=excluded.unit, "
        "device_class=excluded.device_class, expected_min=excluded.expected_min, "
        "expected_max=excluded.expected_max",
        params);
}

int64_t DeviceRegistry::cameraIdForDevice(const std::string& deviceId,
                                          const std::string& name,
                                          const std::string& streamUrl) {
    int64_t camId = -1;
    db_.query("SELECT id FROM cameras WHERE device_id=?1 LIMIT 1", {deviceId},
              [&](const Row& r) { camId = r.i64(0); });
    if (camId >= 0) {
        if (!streamUrl.empty())
            db_.exec("UPDATE cameras SET url=?1 WHERE id=?2", {streamUrl, camId});
        return camId;
    }
    camId = db_.exec(
        "INSERT INTO cameras(name,url,location,enabled,device_id) "
        "VALUES(?1,?2,'',1,?3)",
        {name, streamUrl, deviceId});
    return camId;
}

bool DeviceRegistry::inRange(const std::string& instrumentId, double value) {
    bool ok = true;   // no row / no range => in range
    db_.query("SELECT expected_min, expected_max FROM instruments WHERE id=?1",
              {instrumentId}, [&](const Row& r) {
                  if (!r.isNull(0) && value < r.dbl(0)) ok = false;
                  if (!r.isNull(1) && value > r.dbl(1)) ok = false;
              });
    return ok;
}

std::vector<EdgeDeviceRow> DeviceRegistry::list(const std::string& kindFilter) {
    std::vector<EdgeDeviceRow> out;
    const int64_t now = nowUnix();
    const std::string sql =
        kindFilter.empty()
            ? "SELECT id,kind,name,location,transport,endpoint,capabilities,"
              "fw_version,last_seen,enabled FROM edge_devices ORDER BY kind, name"
            : "SELECT id,kind,name,location,transport,endpoint,capabilities,"
              "fw_version,last_seen,enabled FROM edge_devices WHERE kind=?1 "
              "ORDER BY name";
    std::vector<nlohmann::json> params;
    if (!kindFilter.empty()) params.push_back(kindFilter);
    db_.query(sql, params, [&](const Row& r) {
        EdgeDeviceRow d;
        d.id = r.text(0); d.kind = r.text(1); d.name = r.text(2);
        d.location = r.text(3); d.transport = r.text(4); d.endpoint = r.text(5);
        d.capabilities_json = r.text(6); d.fw_version = r.text(7);
        d.last_seen = r.i64(8); d.enabled = r.i64(9) != 0;
        d.online = (now - d.last_seen) < 90;   // presence window
        out.push_back(std::move(d));
    });
    return out;
}

std::optional<EdgeDeviceRow> DeviceRegistry::get(const std::string& deviceId) {
    std::optional<EdgeDeviceRow> out;
    const int64_t now = nowUnix();
    db_.query("SELECT id,kind,name,location,transport,endpoint,capabilities,"
              "fw_version,last_seen,enabled FROM edge_devices WHERE id=?1",
              {deviceId}, [&](const Row& r) {
                  EdgeDeviceRow d;
                  d.id = r.text(0); d.kind = r.text(1); d.name = r.text(2);
                  d.location = r.text(3); d.transport = r.text(4); d.endpoint = r.text(5);
                  d.capabilities_json = r.text(6); d.fw_version = r.text(7);
                  d.last_seen = r.i64(8); d.enabled = r.i64(9) != 0;
                  d.online = (now - d.last_seen) < 90;
                  out = std::move(d);
              });
    return out;
}

std::string DeviceRegistry::pairedKey(const std::string& deviceId) {
    std::string key;
    db_.query("SELECT paired_key FROM edge_devices WHERE id=?1", {deviceId},
              [&](const Row& r) { key = r.text(0); });
    return key;
}

void DeviceRegistry::setPairedKey(const std::string& deviceId, const std::string& key) {
    db_.exec("UPDATE edge_devices SET paired_key=?1 WHERE id=?2", {key, deviceId});
}

} // namespace polymath
