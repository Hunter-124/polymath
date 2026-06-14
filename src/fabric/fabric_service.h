#pragma once
//
// FabricService — the bridge between autonomous edge devices and Hearth's two
// frozen contracts (the EventBus and the SQLite schema).
//
// It exposes ingest* methods that accept the wire payloads from docs/FABRIC.md,
// persist them via DeviceRegistry, and re-publish them onto the EventBus so the
// rest of the app (WsHub -> mobile, UI models, agent tools, memory) keeps using
// the contracts it already knows.  Nothing downstream needs to learn MQTT.
//
// Two entry paths feed the SAME ingest methods:
//   * HTTP plane  — the gateway routes (announce / camera events / readings)
//                   call ingest* directly. Works with no broker.
//   * MQTT plane  — when built with POLYMATH_USE_MQTT and started, an internal
//                   client subscribes to hearth/# and routes messages to ingest*.
//
// ingest* are thread-safe (Database serializes; EventBus is queued), so either
// path may call them from its own thread.
//
#include "device_registry.h"

#include <nlohmann/json.hpp>
#include <QByteArray>
#include <memory>
#include <string>

namespace polymath {

class Database;

class FabricService {
public:
    explicit FabricService(Database& db);
    ~FabricService();

    // --- ingest (wire payload -> DB + EventBus). All per docs/FABRIC.md. ---

    // §3 announce / discovery. Returns the device id (or "" on bad payload).
    std::string ingestAnnounce(const nlohmann::json& announce);

    // §2 presence (birth/LWT). Emits DevicePresence.
    void ingestPresence(const nlohmann::json& presence);

    // §4 camera event. Decodes the thumbnail to media/events, writes an `events`
    // row (with clip_url/confidence/device_id), emits Detection. cameraId<0 =>
    // resolve from device_id. Returns the new event row id (or -1).
    int64_t ingestCameraEvent(int64_t cameraId, const nlohmann::json& event);

    // §5 instrument reading. Persists a `measurements` row, emits InstrumentReading.
    void ingestReading(const nlohmann::json& reading);

    // Store a raw JPEG as the latest live tile for a camera (POST /cameras/:id/frame).
    void ingestFrame(int64_t cameraId, const QByteArray& jpeg);

    DeviceRegistry& registry() { return registry_; }

    // --- optional MQTT transport (no-op unless built with POLYMATH_USE_MQTT) ---
    // host/port of the broker (the installer bundles one on localhost:1883).
    bool startMqtt(const std::string& host, int port);
    void stopMqtt();
    bool mqttRunning() const;

private:
    Database&      db_;
    DeviceRegistry registry_;

    struct MqttImpl;                 // hidden; only compiled with POLYMATH_USE_MQTT
    std::unique_ptr<MqttImpl> mqtt_;
};

} // namespace polymath
