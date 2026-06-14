# firmware/common — Hearth shared fabric library

A small Arduino/ESP-IDF (Arduino framework) library shared by every ESP32 / ESP32-S3
Hearth device. It implements the device side of [`docs/FABRIC.md`](../../docs/FABRIC.md)
so each firmware project only has to add its sensor/camera specifics.

## Modules

| File | Role | FABRIC.md |
|---|---|---|
| `hearth_id` | `device_id = hearth-<kind3>-<hex6>` from MAC | §1 |
| `hearth_wifi` | STA connect + SoftAP `Hearth-Setup-<hex6>` captive provisioning (NVS) | §6 |
| `hearth_mdns` | advertise `_hearth._tcp` + `<device_id>.local` | §3, §6 |
| `hearth_mqtt` | birth/LWT, announce, event, reading, wake, `cmd/*` dispatch (PubSubClient) | §2–§5, §7, §8 |
| `hearth_httpd` | device HTTP API: `/ /status /snapshot /stream /clips /clips/<f> /provision /pair /config` | §6 |
| `hearth_auth` | per-device 32-byte key (NVS), `HMAC-SHA256(key, path+"."+ts)` bearer verify, pairing QR JSON | §6 |
| `hearth_sdclip` | motion/person-gated MJPEG clip recorder to SD + retention prune + list/serve | §4 |
| `hearth_ota` | `cmd/ota` HTTPS fetch + sha256 verify + flash | §7 |

## Using it from a project

In the project's `platformio.ini`:

```ini
lib_deps =
    knolleary/PubSubClient @ ^2.8
    symlink://../common      ; pull in firmware/common
```

Then `#include "hearth_id.h"` etc. Typical wiring (see any project's `main`):

```cpp
hearth::Auth  auth;   auth.begin();
hearth::Wifi  wifi;   wifi.begin(hearth::Kind::Camera);
if (wifi.isProvisioning()) { /* loop(): wifi.loop(); return; */ }
String id = hearth::deviceId(hearth::Kind::Camera);
hearth::Mdns mdns;    mdns.begin(id, "camera", cfg.name);
hearth::Mqtt mqtt;    mqtt.begin("hub.local", 1883, id, "camera", cfg.name, FW);
hearth::Httpd httpd;  httpd.begin(id, "camera", cfg.name, FW, auth, &clips,
                                  mdns.lanHost(), wifi.softApSsid(), &edgeCfg, hooks, &nowUnix);
```

## Auth scheme (device side)

FABRIC.md §6 says the app authenticates **directly** to the device with
`Authorization: Bearer <HMAC(key, path+ts)>`. Concretely the device verifies
`base64url(HMAC-SHA256(key, path + "." + ts))` against the `Authorization` value,
with `X-Hearth-Ts: <unix>` carrying `ts` and a ±120 s freshness window (skipped
while the device clock is unsynced). The 32-byte `key` is generated from the
hardware RNG on first boot, persisted in NVS, and surfaced once via the pairing QR.
This is the same HMAC-SHA256 family the hub gateway uses (`src/gateway/auth.h`).

## Build

PlatformIO resolves this as a local library; there is nothing to build on its own.
`pio run` inside any consuming project compiles it. Toolchain:
`pip install platformio` then `pio run` in a project dir.

## Notes / FABRIC ambiguities

- **Clip container.** §4's example `clip_url` ends in `.mp4`, but the field is an
  opaque URL and on-MCU MP4 muxing is impractical. We record `<unix>.mjpeg`
  (multipart-JPEG) and serve it as `video/x-motion-jpeg`. The hub/app treat the
  URL opaquely. K230/Pi tiers (with real CPUs) can emit `.mp4` instead.
- **`/pair` token.** §6 says `/pair` exchanges the QR key for a "device token".
  Since the app caches `{device_id, key}` and self-derives per-request HMACs, our
  `/pair` simply confirms the key (returns `HMAC(key,"/pair"+ts)` as a token) —
  there is no separate server-side session to mint.
