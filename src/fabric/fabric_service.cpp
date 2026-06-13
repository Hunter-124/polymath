#include "fabric_service.h"

#include "database.h"
#include "event_bus.h"
#include "logging.h"
#include "paths.h"
#include "types.h"

#include <QByteArray>
#include <QString>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace polymath {

using nlohmann::json;

namespace {
int64_t nowUnix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
std::string str(const json& j, const char* k, const std::string& def = "") {
    if (auto it = j.find(k); it != j.end() && it->is_string()) return it->get<std::string>();
    return def;
}
TimePoint tpFromUnix(int64_t s) {
    return TimePoint(std::chrono::seconds(s));
}
// Decode a base64 JPEG and write it under media/events; returns the file path
// (relative to media/, matching how camera_worker stores thumb_path) or "".
std::string writeThumb(int64_t cameraId, const std::string& b64, int64_t tsMs) {
    if (b64.empty()) return {};
    QByteArray raw = QByteArray::fromBase64(QByteArray::fromStdString(b64));
    if (raw.isEmpty()) return {};
    namespace fs = std::filesystem;
    const fs::path dir = Paths::instance().media() / "events";
    std::error_code ec; fs::create_directories(dir, ec);
    const std::string fname =
        "cam" + std::to_string(cameraId) + "_" + std::to_string(tsMs) + "_edge.jpg";
    const fs::path full = dir / fname;
    std::ofstream f(full, std::ios::binary);
    if (!f) return {};
    f.write(raw.constData(), raw.size());
    if (!f.good()) return {};
    // Stored relative to media/ (serveJpegFile resolves relative paths).
    return (fs::path("events") / fname).string();
}
} // namespace

struct FabricService::MqttImpl {};   // replaced under POLYMATH_USE_MQTT (see mqtt_paho.cpp)

FabricService::FabricService(Database& db) : db_(db), registry_(db) {}
FabricService::~FabricService() { stopMqtt(); }

std::string FabricService::ingestAnnounce(const json& a) {
    const std::string id = registry_.upsertFromAnnounce(a);
    if (id.empty()) return {};
    EventBus::instance().publishDevicePresence(
        DevicePresence{ QString::fromStdString(id),
                        QString::fromStdString(str(a, "kind", "")),
                        QString::fromStdString(str(a, "name", id)),
                        true, nowUnix() });
    return id;
}

void FabricService::ingestPresence(const json& p) {
    const std::string id = str(p, "device_id");
    if (id.empty()) return;
    const bool online = p.value("online", true);
    int64_t ts = p.value("ts", int64_t{0});
    if (ts <= 0) ts = nowUnix();
    registry_.setPresence(id, online, ts);
    EventBus::instance().publishDevicePresence(
        DevicePresence{ QString::fromStdString(id),
                        QString::fromStdString(str(p, "kind", "")),
                        QString::fromStdString(str(p, "name", id)),
                        online, ts });
}

int64_t FabricService::ingestCameraEvent(int64_t cameraId, const json& e) {
    const std::string deviceId = str(e, "device_id");
    const std::string kind     = str(e, "kind", "motion");   // motion|person|face
    const double confidence    = e.value("confidence", 0.0);
    const std::string clipUrl  = str(e, "clip_url");
    int64_t ts = e.value("ts", int64_t{0});
    if (ts <= 0) ts = nowUnix();

    // Resolve the camera row from the device when an explicit id wasn't supplied.
    if (cameraId < 0 && !deviceId.empty()) {
        if (auto d = registry_.get(deviceId))
            cameraId = registry_.cameraIdForDevice(deviceId, d->name, d->endpoint + "/stream");
    }
    if (cameraId < 0) { PM_WARN("fabric: camera event with no resolvable camera"); return -1; }

    if (!deviceId.empty()) registry_.touch(deviceId, ts);

    const std::string thumb = writeThumb(cameraId, str(e, "thumb_b64"), ts * 1000);
    const std::string label = (kind == "person") ? "person"
                            : (kind == "face")   ? "face"
                                                 : "motion";

    const int64_t eventId = db_.exec(
        "INSERT INTO events(kind,camera_id,label,thumb_path,clip_url,confidence,device_id,ts) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7,?8)",
        {kind, cameraId, label, thumb, clipUrl, confidence, deviceId, ts});

    // Re-publish as a Detection so the existing UI/WS/timeline pipeline lights up.
    Detection d;
    d.camera_id = static_cast<int>(cameraId);
    d.ts = tpFromUnix(ts);
    BoundingBox box{0.f, 0.f, 1.f, 1.f, static_cast<float>(confidence), label};
    d.boxes.push_back(box);
    EventBus::instance().publishDetection(d);

    PM_INFO("fabric: camera {} {} event (conf {:.2f}) clip={}",
            cameraId, kind, confidence, clipUrl.empty() ? "-" : clipUrl);
    return eventId;
}

void FabricService::ingestReading(const json& r) {
    const std::string instId = str(r, "instrument_id");
    if (instId.empty()) { PM_WARN("fabric: reading without instrument_id"); return; }
    const std::string deviceId = str(r, "device_id");
    const std::string unit  = str(r, "unit");
    const std::string dclass = str(r, "device_class");
    const double value = r.value("value", 0.0);
    int64_t ts = r.value("ts", int64_t{0});
    if (ts <= 0) ts = nowUnix();

    const bool ok = registry_.inRange(instId, value);
    db_.exec(
        "INSERT INTO measurements(instrument_id,value,unit,in_range,source,ts) "
        "VALUES(?1,?2,?3,?4,'instrument',?5)",
        {instId, value, unit, ok ? 1 : 0, ts});
    if (!deviceId.empty()) registry_.touch(deviceId, ts);

    EventBus::instance().publishInstrumentReading(
        InstrumentReading{ QString::fromStdString(instId),
                           QString::fromStdString(deviceId), value,
                           QString::fromStdString(unit),
                           QString::fromStdString(dclass), ok, ts });
}

void FabricService::ingestFrame(int64_t cameraId, const QByteArray& jpeg) {
    if (jpeg.isEmpty() || cameraId < 0) return;
    Frame f;
    f.camera_id = static_cast<int>(cameraId);
    f.jpeg.assign(reinterpret_cast<const uint8_t*>(jpeg.constData()),
                  reinterpret_cast<const uint8_t*>(jpeg.constData()) + jpeg.size());
    f.ts = std::chrono::system_clock::now();
    EventBus::instance().publishFrame(f);
}

// --- MQTT transport: real impl lives in mqtt_paho.cpp behind POLYMATH_USE_MQTT.
#ifndef POLYMATH_USE_MQTT
bool FabricService::startMqtt(const std::string&, int) {
    PM_INFO("fabric: MQTT transport not compiled in (HTTP plane only). "
            "Rebuild with -DPOLYMATH_USE_MQTT=ON to enable the broker bridge.");
    return false;
}
void FabricService::stopMqtt() {}
bool FabricService::mqttRunning() const { return false; }
#endif

} // namespace polymath
