#pragma once
//
// json_map — the single place that translates between Polymath's internal
// representations (SQLite rows from src/core/schema.h, value types from
// src/core/types.h, EventBus payloads from src/core/event_bus.h) and the wire
// DTOs defined in app/src/api/contract.ts.
//
// Field names here MUST match contract.ts byte-for-byte (snake_case).  If you
// change a DTO there, change it here.  Everything is nlohmann::json (already a
// project dependency via pm_core).
//
#include <nlohmann/json.hpp>

#include "event_bus.h"   // TokenChunk, Notice, TaskEvent, ReminderFired, FindObjectResult, PrivacyChanged, SpeakRequest
#include "types.h"       // Utterance, Detection, BoundingBox

#include <QString>
#include <QVariant>
#include <QVariantList>

namespace polymath {

class Database;
class Row;
struct EdgeDeviceRow;   // device fabric (src/fabric/device_registry.h)

namespace json_map {

using nlohmann::json;

// ─── small helpers ──────────────────────────────────────────────────────────

inline std::string qstr(const QString& s) { return s.toStdString(); }

// Current wall-clock in unix seconds (envelope `ts`).
int64_t nowUnix();

// ─── row → DTO (read straight from the canonical tables) ────────────────────
//
// Each expects a SELECT with the documented column order (see the .cpp / the
// SQL the http_router issues).  Returning by value keeps callers simple.

json shoppingItemFromRow(const Row& r);   // ShoppingItemDTO
json taskFromRow(const Row& r);           // TaskDTO        (params/result parsed from JSON text)
json reminderFromRow(const Row& r);       // ReminderDTO
json cameraFromRow(const Row& r);         // CameraDTO      (adds snapshot_url/stream_url)
json timelineEventFromRow(const Row& r);  // TimelineEventDTO (adds thumb_url when a thumb exists)
json memoryFromRow(const Row& r);         // MemoryDTO
json personalityFromRow(const Row& r);    // PersonalityDTO
json modelFromRow(const Row& r);          // ModelDTO
json deviceFromRow(const Row& r, bool online); // Device (auth/devices table)

// --- device fabric (v2) ----------------------------------------------------
json edgeDeviceToJson(const EdgeDeviceRow& d);  // DeviceDTO (edge_devices)
json instrumentFromRow(const Row& r);           // InstrumentDTO
json labSessionFromRow(const Row& r);           // LabSessionDTO (no steps)

// A QVariantMap as produced by AppController::models() -> ModelDTO.  Lets the
// /models route reuse the bridge instead of re-querying the DB.
json modelFromVariant(const QVariant& v);

// ─── core types → DTO (used by the WS stream) ───────────────────────────────

json tokenEvent(const TokenChunk& t);            // TokenEvent
json noticeEvent(const Notice& n);               // NoticeEvent
json speakEvent(const SpeakRequest& s);          // SpeakEvent
json detectionEvent(const Detection& d);         // DetectionDTO
json utteranceEvent(const Utterance& u);         // (ChatMessageDTO-ish ASR payload)
json findObjectEvent(const FindObjectResult& r); // FindObjectResultDTO
json taskEvent(const TaskEvent& t);              // TaskDTO-lite (id/type/status/detail)
json reminderEvent(const ReminderFired& r);      // { id, text }
json privacyEvent(const PrivacyChanged& p);      // { key, enabled }
json frameEvent(const Frame& f);                 // { camera_id, ts, width, height, jpeg_b64 }
json instrumentReadingEvent(const InstrumentReading& r); // InstrumentReading payload
json devicePresenceEvent(const DevicePresence& p);       // device_presence payload
json labStepEvent(const LabStepEvent& s);                // lab_step payload

// ─── envelope ───────────────────────────────────────────────────────────────
//
// Wraps any payload as the contract's ServerEvent<T>: { type, ts, data }.
// `type` is one of contract.ts ServerEventType.
json serverEvent(const char* type, json data);

// ─── status / pairing ───────────────────────────────────────────────────────

// ServerStatus { listening, active_personality, model_status, privacy{}, uptime_s }.
// privacy is the map of all privacy.* toggles read from the settings table.
json serverStatus(Database& db,
                  bool listening,
                  const std::string& activePersonality,
                  const std::string& modelStatus,
                  int64_t uptimeSeconds);

// ServerCapabilities advertised in the PairResponse (and reusable elsewhere).
json serverCapabilities();

} // namespace json_map
} // namespace polymath
