#pragma once
//
// DeviceRegistry — the persistence layer for the device fabric.  Owns reads and
// writes to the `edge_devices` / `instruments` tables and keeps the legacy
// `cameras` row for a camera device in sync (so the existing vision/timeline/UI
// keep working unchanged).  Pure DB logic — no Qt threads, no networking — so it
// is safe to call from the gateway thread or the MQTT client thread alike
// (Database serializes writes on its own mutex).
//
// See docs/FABRIC.md for the wire shapes these methods consume.
//
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace polymath {

class Database;

struct EdgeDeviceRow {
    std::string id;
    std::string kind;        // camera|voice_sat|instrument|panel
    std::string name;
    std::string location;
    std::string transport;
    std::string endpoint;
    std::string capabilities_json;
    std::string fw_version;
    int64_t     last_seen = 0;
    bool        enabled   = true;
    bool        online    = false;   // derived: seen within the presence window
};

class DeviceRegistry {
public:
    explicit DeviceRegistry(Database& db) : db_(db) {}

    // Upsert from an announce/presence payload (docs/FABRIC.md §2/§3). Creates or
    // updates the edge_devices row and, for kind=="camera", ensures a matching
    // `cameras` row (url = endpoint + "/stream"). Returns the device id.
    std::string upsertFromAnnounce(const nlohmann::json& announce);

    // Mark a device online/offline + bump last_seen (presence birth/LWT).
    void setPresence(const std::string& deviceId, bool online, int64_t ts);

    // Refresh last_seen to "now" — call on any telemetry from the device.
    void touch(const std::string& deviceId, int64_t ts);

    // Upsert one instrument channel (from an announce `instruments[]` entry).
    void upsertInstrument(const nlohmann::json& inst, const std::string& deviceId);

    // The camera row id linked to this device, creating one if needed. Returns -1
    // if the device isn't a camera / can't be resolved.
    int64_t cameraIdForDevice(const std::string& deviceId,
                              const std::string& name,
                              const std::string& streamUrl);

    // Range check for a reading against the instrument's expected_min/max.
    // Returns true when no range is configured (nothing to violate).
    bool inRange(const std::string& instrumentId, double value);

    std::vector<EdgeDeviceRow> list(const std::string& kindFilter = "");
    std::optional<EdgeDeviceRow> get(const std::string& deviceId);

    // Per-device shared secret used for direct mobile pairing (FABRIC.md §6).
    std::string pairedKey(const std::string& deviceId);
    void        setPairedKey(const std::string& deviceId, const std::string& key);

private:
    Database& db_;
};

} // namespace polymath
