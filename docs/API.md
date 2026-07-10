# Polymath Remote API (v1)

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

> **Stream note:** live MJPEG (`multipart/x-mixed-replace`) is not tunnelled over
> the relay's buffered request/response path; off-LAN the app polls
> `/snapshot` (~2 s). On the LAN the gateway can serve true MJPEG. Live video is
> better delivered as `frame` events on the WebSocket.

## WebSocket — `GET /api/v1/events?token=…`

One envelope; `type` discriminates the payload (mirrors the EventBus signals in
`src/core/event_bus.h`):

```jsonc
{ "type": "token" | "notice" | "utterance" | "detection" | "frame"
        | "find_object" | "task" | "reminder" | "privacy" | "status" | "speak",
  "ts": 1733616000,
  "data": { /* per-type payload */ } }
```

Key payloads: `token {request_id, text, done}` (assistant streaming),
`notice {level, source, message}`, `detection {camera_id, boxes[], user_id?, ts}`,
`task {task_id, type, status, detail}`, `reminder {reminder_id, text}`,
`find_object {query, answer, camera_id, ts}`, `speak {text, voice, request_id, audio_url?}`.

**Client → server control:**

```jsonc
{ "type": "subscribe" | "unsubscribe" | "ping",
  "topics": ["token", "detection", …],
  "camera_ids": [1, 2] }    // for `frame` events
```

Clients subscribe only to the topics a screen needs (e.g. Chat → `token`,
Cameras → `find_object` + `detection`), keeping the stream lean.
