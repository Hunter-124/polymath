# Hearth Remote API (v1)

The REST + WebSocket surface the mobile/web client uses. The **source of truth**
is [`app/src/api/contract.ts`](../app/src/api/contract.ts) (TypeScript types +
the `ENDPOINTS` map); the gateway mirrors it in `src/gateway/json_map.cpp`. All
JSON is snake_case and mirrors `src/core/types.h` / `src/core/schema.h`.

* **Base:** `…/api/v1`. LAN: `http://polymath.local:8765`. Relay:
  `https://<relay>/h/<home_id>`.
* **Auth:** `Authorization: Bearer <device-token>` on every route except
  `POST /pair` and `GET /status`. Media GETs also accept `?token=`. A 401
  auto-unpairs the client.

## REST

| Method | Path | Body / query | Returns |
| --- | --- | --- | --- |
| POST | `/pair` | `{code, device_name, platform, pubkey?}` | `PairResponse {token, device_id, role, home_id, relay_url, capabilities}` |
| GET | `/me` | — | the calling device |
| GET | `/status` | — | `ServerStatus {listening, active_personality, model_status, privacy{}, uptime_s}` |
| POST | `/chat` | `{text, personality?}` | `{request_id}` — reply streams as `token` events |
| GET | `/chat/history` | `?limit=` | `ChatMessageDTO[]` |
| POST | `/voice` | audio blob | *(reserved — ASR → chat)* |
| GET | `/cameras` | — | `CameraDTO[]` |
| GET | `/cameras/:id/snapshot` | `?token=` | JPEG (latest frame) |
| GET | `/cameras/:id/stream` | `?token=` | JPEG/MJPEG (see note) |
| POST | `/find-object` | `{query}` | `FindObjectResultDTO` (also a `find_object` event) |
| GET / POST | `/tasks` | `TaskCreateRequest` | `TaskDTO[]` / `TaskDTO` |
| PATCH | `/tasks/:id` | `{status?, priority?}` | `TaskDTO` |
| GET / POST | `/reminders` | `ReminderCreateRequest` | `ReminderDTO[]` / `ReminderDTO` |
| DELETE | `/reminders/:id` | — | 204 |
| GET / POST | `/shopping` | `ShoppingCreateRequest` | `ShoppingItemDTO[]` / `ShoppingItemDTO` |
| PATCH / DELETE | `/shopping/:id` | `{done}` | `ShoppingItemDTO` / 204 |
| GET | `/timeline` | `?since=` | `TimelineEventDTO[]` |
| GET / POST | `/memory` | `?q=` / `{text}` | `MemoryDTO[]` / `MemoryDTO` |
| GET | `/personalities` | — | `PersonalityDTO[]` |
| POST | `/personalities/active` | `{name}` | 204 |
| GET | `/models` | — | `ModelDTO[]` |
| GET | `/settings` | — | `Record<string,string>` |
| PATCH | `/settings/:key` | `{value}` | 204 |
| GET | `/devices` | — | `Device[]` (owner) |
| DELETE | `/devices/:id` | — | 204 (revoke) |
| GET | `/fabric/devices` | `?kind=camera\|voice_sat\|instrument\|panel` | `EdgeDeviceDTO[]` |
| GET | `/fabric/devices/:id` | — | `EdgeDeviceDTO` |
| POST | `/fabric/devices/announce` | `DeviceAnnounce` (see below) | `{device_id}` 201 |
| DELETE | `/fabric/devices/:id` | — | 204 |
| GET | `/instruments` | — | `InstrumentDTO[]` |
| GET | `/instruments/:id/read` | — | `ReadingDTO` (latest retained reading) |
| GET | `/lab/sessions` | — | `LabSessionDTO[]` |
| GET | `/lab/sessions/:id` | — | `LabSessionDTO` with `steps: LabStepDTO[]` |
| POST | `/lab/sessions` | `{title, objective?}` | `LabSessionDTO` 201 |
| POST | `/cameras/:id/events` | `CameraEventDTO` (see below) | `{event_id}` 201 — fabric ingest (§4) |
| POST | `/cameras/:id/frame` | raw JPEG body | 204 — live UI tile push |

> **Stream note:** live MJPEG (`multipart/x-mixed-replace`) is not tunnelled over
> the relay's buffered request/response path; off-LAN the app polls
> `/snapshot` (~2 s). On the LAN the gateway can serve true MJPEG. Live video is
> better delivered as `frame` events on the WebSocket.

### Fabric payload shapes

**`DeviceAnnounce`** (POST `/fabric/devices/announce`):

```jsonc
{
  "device_id": "hearth-cam-a1b2c3",
  "kind": "camera",            // camera|voice_sat|instrument|panel
  "name": "Front Door",
  "location": "Entry",         // optional
  "fw": "0.2.0",               // optional
  "endpoint": "http://192.168.1.42",   // device's own HTTP base
  "transport": "mqtt",         // mqtt|http|mjpeg|rtsp
  "capabilities": { "stream": true, "clips": true },  // optional
  "instruments": [             // only for kind=instrument
    { "id": "hmm_a1b2_balance_mass_g", "device_id": "hearth-hmm-44ccbb",
      "name": "Balance", "channel": 0, "unit": "g",
      "device_class": "mass", "expected_min": 0, "expected_max": 500 }
  ]
}
```

**`CameraEventDTO`** (POST `/cameras/:id/events`):

```jsonc
{
  "device_id": "hearth-cam-a1b2c3",   // optional; resolved from :id otherwise
  "kind": "person",                    // motion|person|face
  "confidence": 0.86,                  // optional
  "thumb_b64": "<jpeg base64 ≤480px>", // optional
  "clip_url": "http://192.168.1.42/clips/1718246542.mp4",  // optional
  "ts": 0                              // optional; hub re-stamps on receipt
}
```

The hub writes an `events` row (including the new `clip_url`, `confidence`, and
`device_id` columns) and emits a `detection` WebSocket event. See
[`docs/FABRIC.md`](FABRIC.md) §4 for the full camera event contract.

## WebSocket — `GET /api/v1/events?token=…`

One envelope; `type` discriminates the payload (mirrors the EventBus signals in
`src/core/event_bus.h`):

```jsonc
{ "type": "token" | "notice" | "utterance" | "detection" | "frame"
        | "find_object" | "task" | "reminder" | "privacy" | "status" | "speak"
        | "instrument_reading" | "device_presence" | "lab_step",
  "ts": 1733616000,
  "data": { /* per-type payload */ } }
```

Key payloads: `token {request_id, text, done}` (assistant streaming),
`notice {level, source, message}`, `detection {camera_id, boxes[], user_id?, ts}`,
`task {task_id, type, status, detail}`, `reminder {reminder_id, text}`,
`find_object {query, answer, camera_id, ts}`,
`speak {text, voice, request_id, target?, audio_url?}` (`target` is a
voice-satellite id when the hub routes TTS to a room; empty = local speaker).

**v2 fabric event types:**

| type | data shape | when |
|---|---|---|
| `instrument_reading` | `{instrument_id, device_id, value, unit, device_class, in_range, ts}` | `FabricService` receives a reading via MQTT or `POST /instruments/:id/read` |
| `device_presence` | `{device_id, kind, name, online, ts}` | edge device comes online (MQTT birth) or goes offline (LWT / mDNS timeout) |
| `lab_step` | `{session_id, step_no, prompt, status, measured_value, unit, verified}` | lab-session agent advances a step; `status` ∈ `ask\|verifying\|verified\|out_of_range\|done` |

**Client → server control:**

```jsonc
{ "type": "subscribe" | "unsubscribe" | "ping",
  "topics": ["token", "detection", …],
  "camera_ids": [1, 2] }    // for `frame` events
```

Clients subscribe only to the topics a screen needs (e.g. Chat → `token`,
Cameras → `find_object` + `detection`), keeping the stream lean.
