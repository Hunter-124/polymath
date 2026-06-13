#include "json_map.h"

#include "config.h"            // keys::*
#include "database.h"          // Database, Row
#include "device_registry.h"   // EdgeDeviceRow (device fabric)

#include <QByteArray>
#include <chrono>

namespace polymath {
namespace json_map {

using nlohmann::json;

int64_t nowUnix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               Clock::now().time_since_epoch())
        .count();
}

// API base, kept in sync with contract.ts (API_BASE = "/api/v1").  Media URLs
// embedded in DTOs are relative to this so they work over LAN and relay alike.
static constexpr const char* kApiBase = "/api/v1";

// Parse a JSON-typed text column; fall back to a default node if it's empty or
// malformed (the DB stores '{}' / '' for these).
static json parseJsonText(const std::string& s, json fallback) {
    if (s.empty()) return fallback;
    try {
        return json::parse(s);
    } catch (...) {
        return fallback;
    }
}

// ─── row → DTO ──────────────────────────────────────────────────────────────
//
// Column orders below are the contract between these readers and the SELECTs in
// http_router.cpp.  Keep them in lockstep.

// SELECT id, item, quantity, done, created_at FROM shopping_items
json shoppingItemFromRow(const Row& r) {
    return json{
        {"id",         r.i64(0)},
        {"item",       r.text(1)},
        {"quantity",   r.text(2)},
        {"done",       r.i64(3) != 0},
        {"created_at", r.i64(4)},
    };
}

// SELECT id, type, params_json, priority, status, result_json, created_at, updated_at FROM tasks
json taskFromRow(const Row& r) {
    json result = json::value_t::null;
    std::string rj = r.text(5);
    if (!rj.empty()) result = parseJsonText(rj, json(rj));   // arbitrary JSON, else raw string
    return json{
        {"id",         r.i64(0)},
        {"type",       r.text(1)},
        {"params",     parseJsonText(r.text(2), json::object())},
        {"priority",   r.i64(3)},
        {"status",     r.text(4)},
        {"result",     result},
        {"created_at", r.i64(6)},
        {"updated_at", r.i64(7)},
    };
}

// SELECT id, text, due_at, rrule, condition, fired, created_at FROM reminders
json reminderFromRow(const Row& r) {
    json j{
        {"id",         r.i64(0)},
        {"text",       r.text(1)},
        {"rrule",      r.text(3)},
        {"condition",  r.text(4)},
        {"fired",      r.i64(5) != 0},
        {"created_at", r.i64(6)},
    };
    // due_at is nullable (condition-based reminders have none).
    if (r.isNull(2)) j["due_at"] = json::value_t::null;
    else             j["due_at"] = r.i64(2);
    return j;
}

// SELECT id, name, location, enabled FROM cameras
json cameraFromRow(const Row& r) {
    const int64_t id = r.i64(0);
    const std::string base = std::string(kApiBase) + "/cameras/" + std::to_string(id);
    return json{
        {"id",           id},
        {"name",         r.text(1)},
        {"location",     r.text(2)},
        {"enabled",      r.i64(3) != 0},
        // Always gateway-proxied so the client never touches the raw camera URL
        // and remote access keeps working off-LAN.
        {"snapshot_url", base + "/snapshot"},
        {"stream_url",   base + "/stream"},
    };
}

// SELECT id, kind, camera_id, user_id, label, thumb_path, ts, clip_url, confidence FROM events
json timelineEventFromRow(const Row& r) {
    const int64_t id = r.i64(0);
    json j{
        {"id",    id},
        {"kind",  r.text(1)},
        {"label", r.text(4)},
        {"ts",    r.i64(6)},
    };
    if (!r.isNull(2)) j["camera_id"] = r.i64(2);
    if (!r.isNull(3)) j["user_id"]   = r.i64(3);
    // Only advertise a thumbnail URL when a thumb actually exists on disk-record.
    if (!r.text(5).empty())
        j["thumb_url"] = std::string(kApiBase) + "/timeline/" + std::to_string(id) + "/thumb";
    // Edge clips (FABRIC.md §4): clip lives on the camera's SD, served by its URL.
    if (!r.isNull(7) && !r.text(7).empty()) j["clip_url"]   = r.text(7);
    if (!r.isNull(8) && r.dbl(8) > 0.0)     j["confidence"] = r.dbl(8);
    return j;
}

// SELECT id, kind, text, source, user_id, ts FROM memories
json memoryFromRow(const Row& r) {
    json j{
        {"id",     r.i64(0)},
        {"kind",   r.text(1)},
        {"text",   r.text(2)},
        {"source", r.text(3)},
        {"ts",     r.i64(5)},
    };
    if (!r.isNull(4)) j["user_id"] = r.i64(4);
    return j;
}

// SELECT name, voice, wake_phrase, is_active FROM personalities
json personalityFromRow(const Row& r) {
    return json{
        {"name",        r.text(0)},
        {"voice",       r.text(1)},
        {"wake_phrase", r.text(2)},
        {"active",      r.i64(3) != 0},
    };
}

// SELECT id, display_name, role, path, n_ctx, n_gpu_layers, is_active FROM models
json modelFromRow(const Row& r) {
    return json{
        {"id",           r.text(0)},
        {"display_name", r.text(1)},
        {"role",         r.text(2)},
        {"path",         r.text(3)},
        {"n_ctx",        r.i64(4)},
        {"n_gpu_layers", r.i64(5)},
        {"active",       r.i64(6) != 0},
    };
}

// SELECT id, name, role, platform, created_at, last_seen FROM devices
json deviceFromRow(const Row& r, bool online) {
    return json{
        {"device_id",  r.text(0)},
        {"name",       r.text(1)},
        {"role",       r.text(2)},
        {"platform",   r.text(3)},
        {"created_at", r.i64(4)},
        {"last_seen",  r.i64(5)},
        {"online",     online},
    };
}

// --- device fabric (v2) ----------------------------------------------------

json edgeDeviceToJson(const EdgeDeviceRow& d) {
    return json{
        {"device_id",    d.id},
        {"kind",         d.kind},
        {"name",         d.name},
        {"location",     d.location},
        {"transport",    d.transport},
        {"endpoint",     d.endpoint},
        {"capabilities", parseJsonText(d.capabilities_json, json::object())},
        {"fw_version",   d.fw_version},
        {"last_seen",    d.last_seen},
        {"enabled",      d.enabled},
        {"online",       d.online},
    };
}

// SELECT id, device_id, name, channel, unit, device_class, expected_min, expected_max FROM instruments
json instrumentFromRow(const Row& r) {
    json j{
        {"id",           r.text(0)},
        {"device_id",    r.text(1)},
        {"name",         r.text(2)},
        {"channel",      r.i64(3)},
        {"unit",         r.text(4)},
        {"device_class", r.text(5)},
    };
    j["expected_min"] = r.isNull(6) ? json(nullptr) : json(r.dbl(6));
    j["expected_max"] = r.isNull(7) ? json(nullptr) : json(r.dbl(7));
    return j;
}

// SELECT id, title, objective, status, report_doc_id, started_at, ended_at FROM lab_sessions
json labSessionFromRow(const Row& r) {
    json j{
        {"id",         r.i64(0)},
        {"title",      r.text(1)},
        {"objective",  r.text(2)},
        {"status",     r.text(3)},
        {"started_at", r.i64(5)},
    };
    j["report_doc_id"] = r.isNull(4) ? json(nullptr) : json(r.i64(4));
    j["ended_at"]      = r.isNull(6) ? json(nullptr) : json(r.i64(6));
    return j;
}

json modelFromVariant(const QVariant& v) {
    const QVariantMap m = v.toMap();
    return json{
        {"id",           qstr(m.value("id").toString())},
        {"display_name", qstr(m.value("displayName").toString())},
        {"role",         qstr(m.value("role").toString())},
        {"path",         qstr(m.value("path").toString())},
        {"n_ctx",        m.value("nCtx").toInt()},
        {"n_gpu_layers", m.value("nGpuLayers").toInt()},
        {"active",       m.value("active").toBool()},
    };
}

// ─── core types → DTO (WS stream payloads) ──────────────────────────────────

json tokenEvent(const TokenChunk& t) {
    return json{
        {"request_id", qstr(t.request_id)},
        {"text",       qstr(t.text)},
        {"done",       t.done},
    };
}

json noticeEvent(const Notice& n) {
    return json{
        {"level",   qstr(n.level)},
        {"source",  qstr(n.source)},
        {"message", qstr(n.message)},
    };
}

json speakEvent(const SpeakRequest& s) {
    return json{
        {"text",       qstr(s.text)},
        {"voice",      qstr(s.voice)},
        {"request_id", qstr(s.request_id)},
        {"target",     qstr(s.target)},   // "" => local speaker; else a satellite id
        // audio_url is omitted: TTS is rendered client-side from this payload.
    };
}

json detectionEvent(const Detection& d) {
    json boxes = json::array();
    for (const auto& b : d.boxes) {
        boxes.push_back(json{
            {"x",     b.x},
            {"y",     b.y},
            {"w",     b.w},
            {"h",     b.h},
            {"score", b.score},
            {"label", b.label},
        });
    }
    json j{
        {"camera_id", d.camera_id},
        {"boxes",     std::move(boxes)},
        {"ts",        to_unix(d.ts)},
    };
    if (d.user_id) j["user_id"] = *d.user_id;
    return j;
}

json utteranceEvent(const Utterance& u) {
    json j{
        {"text",       u.text},
        {"is_ambient", u.is_ambient},
        {"confidence", u.confidence},
        {"ts",         to_unix(u.ts)},
    };
    if (u.speaker_id) j["speaker_id"] = *u.speaker_id;
    return j;
}

json findObjectEvent(const FindObjectResult& r) {
    return json{
        {"query",     qstr(r.query)},
        {"answer",    qstr(r.answer)},
        {"camera_id", r.camera_id},
        {"ts",        r.ts},
    };
}

json taskEvent(const TaskEvent& t) {
    return json{
        {"id",     t.task_id},
        {"type",   qstr(t.type)},
        {"status", qstr(t.status)},
        {"detail", qstr(t.detail)},
    };
}

json reminderEvent(const ReminderFired& r) {
    return json{
        {"id",   r.reminder_id},
        {"text", qstr(r.text)},
    };
}

json privacyEvent(const PrivacyChanged& p) {
    return json{
        {"key",     qstr(p.key)},
        {"enabled", p.enabled},
    };
}

json frameEvent(const Frame& f) {
    // Thumbnail JPEG as base64 so it rides the JSON WS channel; clients render it
    // directly.  Only ever sent for cameras a client explicitly subscribed to.
    QByteArray jpeg(reinterpret_cast<const char*>(f.jpeg.data()),
                    static_cast<int>(f.jpeg.size()));
    return json{
        {"camera_id", f.camera_id},
        {"width",     f.width},
        {"height",    f.height},
        {"ts",        to_unix(f.ts)},
        {"jpeg_b64",  jpeg.toBase64().toStdString()},
    };
}

json instrumentReadingEvent(const InstrumentReading& r) {
    return json{
        {"instrument_id", qstr(r.instrument_id)},
        {"device_id",     qstr(r.device_id)},
        {"value",         r.value},
        {"unit",          qstr(r.unit)},
        {"device_class",  qstr(r.device_class)},
        {"in_range",      r.in_range},
        {"ts",            r.ts},
    };
}

json devicePresenceEvent(const DevicePresence& p) {
    return json{
        {"device_id", qstr(p.device_id)},
        {"kind",      qstr(p.kind)},
        {"name",      qstr(p.name)},
        {"online",    p.online},
        {"ts",        p.ts},
    };
}

json labStepEvent(const LabStepEvent& s) {
    return json{
        {"session_id",     s.session_id},
        {"step_no",        s.step_no},
        {"prompt",         qstr(s.prompt)},
        {"status",         qstr(s.status)},
        {"measured_value", s.measured_value},
        {"unit",           qstr(s.unit)},
        {"verified",       s.verified},
    };
}

// ─── envelope ───────────────────────────────────────────────────────────────

json serverEvent(const char* type, json data) {
    return json{
        {"type", type},
        {"ts",   nowUnix()},
        {"data", std::move(data)},
    };
}

// ─── status / pairing ───────────────────────────────────────────────────────

json serverStatus(Database& db,
                  bool listening,
                  const std::string& activePersonality,
                  const std::string& modelStatus,
                  int64_t uptimeSeconds) {
    // Surface every privacy.* toggle as a bool map.
    json privacy = json::object();
    db.query("SELECT key, value FROM settings WHERE key LIKE 'privacy.%'", {},
             [&](const Row& r) {
                 const std::string v = r.text(1);
                 privacy[r.text(0)] = (v == "1" || v == "true");
             });
    return json{
        {"listening",          listening},
        {"active_personality", activePersonality},
        {"model_status",       modelStatus},
        {"privacy",            std::move(privacy)},
        {"uptime_s",           uptimeSeconds},
    };
}

json serverCapabilities() {
    return json{
        {"chat",          true},
        {"voice",         true},
        {"cameras",       true},
        {"vision_find",   true},
        {"memory",        true},
        {"tasks",         true},
        {"reminders",     true},
        {"shopping",      true},
        {"personalities", true},
        {"e2e",           false},     // E2E hook points exist but it's off by default
        {"app_version",   "v1"},
    };
}

} // namespace json_map
} // namespace polymath
