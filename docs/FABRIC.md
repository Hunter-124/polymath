# Hearth Device Fabric — wire contract (v1)

The fabric is how autonomous edge devices (cameras, voice satellites, lab instruments,
panels) talk to the Hearth hub **and** how they keep working when the hub is absent.
This document is the frozen contract shared by firmware (`firmware/*`), the hub
(`src/fabric/`, `src/gateway/`), and the mobile app (`app/src/api/`).

Two transport planes:

- **Control/telemetry plane — MQTT** (small JSON messages: presence, detections,
  readings, commands). The hub bundles an embedded broker on `:1883`.
- **Media/bulk plane — HTTP** on the device itself (snapshots, MJPEG, recorded
  clips) and **UDP/Opus** for live satellite audio. Bulk data is *pulled* from the
  device by its URL, never shoved through MQTT.

A device that never sees a hub is still fully usable: it runs its own HTTP server and
pairs directly with the mobile app (see §6).

---

## 1. Identity & topics

- `device_id` — stable, derived from MAC: `hearth-<kind3>-<hex6>`, e.g.
  `hearth-cam-a1b2c3`, `hearth-mic-0f9e8d`, `hearth-hmm-44ccbb`.
- `kind` ∈ `camera | voice_sat | instrument | panel`.
- All topics are rooted at `hearth/<device_id>/...`. Hub → device commands use
  `hearth/<device_id>/cmd/<name>`.

| Topic | Dir | Retained | Payload |
|---|---|---|---|
| `hearth/<id>/status` | dev→hub | yes (LWT) | Presence (§2) |
| `hearth/<id>/announce` | dev→hub | no | Announce (§3) — also mirrors HTTP announce |
| `hearth/<id>/event` | dev→hub | no | CameraEvent (§4) |
| `hearth/<id>/reading/<inst>` | dev→hub | yes | Reading (§5) |
| `hearth/<id>/wake` | dev→hub | no | `{ "phrase": "...", "ts": 0 }` (voice sat) |
| `hearth/<id>/cmd/<name>` | hub→dev | no | command-specific (§7) |

All timestamps `ts` are **unix seconds**; the hub clock is authoritative and re-stamps
on receipt (devices may send `0` when unsynced).

---

## 2. Presence (birth / LWT)

Published retained to `hearth/<id>/status`. The MQTT **Last-Will** is the same topic
with `{"online":false}` so an ungraceful drop still marks the device offline.

```json
{ "device_id": "hearth-cam-a1b2c3", "kind": "camera", "name": "Front Door",
  "online": true, "fw": "0.2.0", "ts": 0 }
```

→ hub `EventBus::publishDevicePresence(DevicePresence{...})` and upserts `devices`.

---

## 3. Announce / discovery

On boot (and via mDNS `_hearth._tcp`), a device announces its capabilities. MQTT topic
`hearth/<id>/announce`, or HTTP `POST /api/v1/devices/announce` for MQTT-less devices.

```json
{ "device_id": "hearth-cam-a1b2c3", "kind": "camera", "name": "Front Door",
  "location": "Entry", "fw": "0.2.0",
  "endpoint": "http://192.168.1.42",          // device's own HTTP base
  "transport": "mqtt",
  "capabilities": {
    "stream": true, "snapshot": true, "clips": true,
    "person_detect": "reliable",               // none|trigger|reliable
    "resolution": "1600x1200", "sd": true
  },
  "instruments": [                              // only for kind=instrument
    { "id": "hearth-hmm-44ccbb_mass_g", "name": "Balance", "channel": 0,
      "unit": "g", "device_class": "mass", "expected_min": 0, "expected_max": 500 }
  ] }
```

→ hub upserts `devices` (+ `instruments` rows). Cameras also get/keep a `cameras` row
linked by `device_id`.

---

## 4. Camera event (on-device person filter → only people kept)

When the device's on-device detector fires (or motion-only on legacy), it records a
clip to SD and publishes an event. The clip is **served from the device**; the hub
stores the URL + a small thumbnail.

```json
{ "device_id": "hearth-cam-a1b2c3", "kind": "person",   // motion|person|face
  "confidence": 0.86,
  "thumb_b64": "<jpeg base64, <=480px>",
  "clip_url": "http://192.168.1.42/clips/1718246542.mp4",
  "ts": 0 }
```

Transports: MQTT `hearth/<id>/event`, **or** HTTP `POST /api/v1/cameras/:id/events`
(same JSON body; for devices that prefer HTTP push). Either path →
`EventBus::publishDetection` + an `events` row with `clip_url`, `confidence`,
`device_id`, `thumb_path`. Live UI tiles may also be fed via
`POST /api/v1/cameras/:id/frame` (raw JPEG body).

**Clip container:** `clip_url` is an **opaque URL** — the hub never parses it. MCU
tiers (ESP32/ESP32-S3/K230) record `<unix>.mjpeg` (MP4 muxing is impractical on an
MCU); the Pi tier writes `.mp4`. Clients just play whatever the URL serves.

**Net-0 storage guarantee:** clips live on the camera's SD card; the hub keeps only
metadata + a thumbnail unless the user opts into hub-side archival.

---

## 5. Instrument reading

Published retained to `hearth/<id>/reading/<instrument_id>`:

```json
{ "instrument_id": "hearth-hmm-44ccbb_mass_g", "device_id": "hearth-hmm-44ccbb",
  "value": 4.213, "unit": "g", "device_class": "mass", "ts": 0 }
```

→ hub `EventBus::publishInstrumentReading`, persists to `measurements`, sets `in_range`
by comparing to `instruments.expected_min/max`. The `read_instrument` agent tool reads
the latest retained value; `record_measurement` writes voice/manual values with the
same shape (`source:"voice"`).

Payload shape intentionally matches Home-Assistant MQTT-discovery sensor fields
(`unit`, `device_class`, `unique_id`) so ESPHome modules map 1:1.

---

## 6. SoftAP → LAN pairing (works with no hub)

First boot, no Wi-Fi creds: device starts a SoftAP `Hearth-Setup-<hex6>` and serves a
captive page + `POST /provision {ssid, pass}`. It also renders a **pairing QR** (on its
status page / e-ink if present) encoding:

```json
{ "v": 1, "device_id": "hearth-cam-a1b2c3", "kind": "camera",
  "key": "<32-byte base64 per-device secret>",
  "softap": "Hearth-Setup-a1b2c3",            // for off-grid direct mode
  "lan_host": "hearth-cam-a1b2c3.local" }     // mDNS name after it joins Wi-Fi
```

The mobile app stores `{device_id, key}` and authenticates **directly** to the device's
HTTP API. There is **no server session** — the app derives a fresh per-request token:

```
X-Hearth-Ts: <unix seconds>
Authorization: Bearer <base64url( HMAC-SHA256(key, path + "." + ts) )>
```

The device recomputes the HMAC and accepts it inside a **±120 s freshness window**
(the window check is skipped while the device's own clock is still unsynced after
boot). `POST /pair` validates the QR `key` and echoes `HMAC(key, "/pair." + ts)` back
as a confirmation handshake (it mints nothing — the app already holds the key). This
mirrors the HMAC scheme the hub gateway uses (`src/gateway/auth.h`), so cameras and
hub speak one auth language. After provisioning, the device joins home Wi-Fi and is
reachable at `lan_host`; SoftAP remains available as an off-grid fallback.

Device HTTP API (port 80): `GET /` (status page), `GET /status`, `GET /snapshot`,
`GET /stream` (MJPEG), `GET /clips` (JSON list), `GET /clips/<file>`, `POST /provision`,
`POST /pair` (exchange QR `key` → device token), `GET /config`, `POST /config`.

---

## 7. Hub → device commands (`hearth/<id>/cmd/<name>`)

| name | payload | effect |
|---|---|---|
| `config` | `{ "person_threshold": 0.6, "retention_days": 14, "face": false }` | push edge config |
| `tts` | `{ "audio_url": "http://hub:8765/.../tts.wav" }` | voice sat plays TTS |
| `identify` | `{}` | blink LED / chirp to locate the device |
| `ota` | `{ "url": "https://.../fw.bin", "sha256": "..." }` | firmware update |

---

## 8. Voice satellite audio plane (UDP/Opus)

Satellites gate on-device with **microWakeWord**, then stream **16 kHz mono** audio to
the hub. Default **Opus** @ 24 kbps, 20 ms frames, over UDP to `hub:8770`; raw 16-bit
PCM is the fallback. Each datagram is prefixed with a 4-byte little-endian header:

```
[ uint16 device_seq ][ uint8 codec (0=pcm16,1=opus) ][ uint8 room_id ]  payload...
```

The hub's `NetworkAudioSource` decodes into the existing lock-free `FloatRing`; the
resulting `Utterance.source = device_id`, so TTS `SpeakRequest.target` routes the reply
back to the originating satellite via `cmd/tts`. Wake events also post to
`hearth/<id>/wake` for UI "which room is talking" cues.

---

## Versioning

`"v"` (where present) and the MQTT `fw` field gate forward-compat. New optional fields
are additive; consumers ignore unknown keys. Breaking changes bump this doc's version
and the announce `"v"`.
