# cam-esp32s3 — Hearth Budget cam

On-device person detection camera on the **Seeed Studio XIAO ESP32-S3 Sense**
(OV2640 + 8 MB PSRAM + microSD). Captures frames, gates on motion, runs an
on-device person model (ESP-DL), records a clip to SD on a person, and publishes
a `CameraEvent` (FABRIC.md §4). Fully standalone: device HTTP API + MQTT + mDNS +
SoftAP provisioning + HMAC direct-pair, all from `firmware/common`.

## Hardware
- Seeed Studio **XIAO ESP32-S3 Sense** (the version with the camera + SD expansion).
- A microSD card (FAT32) for clip storage.
- USB-C for flashing.

## Detection tiers
- **Default build (this repo):** the ESP-DL person model blob is *not* bundled, so
  `EspDlPersonDetector` compiles as a documented stub and the pipeline runs the
  **motion gate** instead. Events publish as `kind:"motion"` and announce
  `person_detect:"trigger"`. See `person_detector.cpp` `TODO(model)`.
- **With the model:** add Espressif **esp-dl** (or an Edge-Impulse FOMO / Swift-YOLO
  person export) to the build, wire it into `EspDlPersonDetector`, and define
  `HEARTH_HAVE_ESPDL` in `config.h`. Events then publish `kind:"person"` with the
  model confidence and announce `person_detect:"reliable"`. Threshold is
  configurable live via `cmd/config` or `POST /config` (default 0.60, conservative).

## Configure
```
cp config.example.h config.h     # already done in-repo with placeholder values
```
Edit `config.h`: device name/location and `HEARTH_MQTT_HOST`. Wi-Fi creds are NOT
in config.h — they are set at runtime via SoftAP provisioning (below).

## Build / flash
```
pip install platformio
pio run                       # compile
pio run -t upload             # flash over USB-C
pio device monitor            # serial @115200
```
Board: `seeed_xiao_esp32s3`. Partition scheme: `partitions.csv` (dual-app OTA on
8 MB flash). PSRAM is enabled via `board_build.arduino.memory_type = qio_opi`.

## First-boot pairing (no hub needed)
1. On first boot (no Wi-Fi creds) the device starts AP **`Hearth-Setup-<hex6>`**.
2. Join it, open `http://192.168.4.1/`, submit your home SSID/password
   (`POST /provision`). The device persists creds to NVS and reboots into STA.
3. It is now reachable at `http://hearth-cam-<hex6>.local/`. The status page shows
   the pairing QR JSON (`{v,device_id,kind,key,softap,lan_host}`) — scan it in the
   Hearth app to store `{device_id, key}` for direct HMAC auth (FABRIC.md §6).

## Endpoints (port 80)
`GET /` `GET /status` · `GET /snapshot`* · `GET /stream`* (MJPEG) ·
`GET /clips` * (JSON) · `GET /clips/<file>`* · `POST /provision` · `POST /pair` ·
`GET /config` · `POST /config`*  — *require `Authorization: Bearer <HMAC(key,path+ts)>`
+ `X-Hearth-Ts`.

## Verify without hardware
`pio run` compiles the firmware (correct-by-construction here; no board needed).
The MQTT/HTTP payloads match FABRIC.md and can be exercised against the hub's
broker exactly like the legacy `esp32cam`. Real-board verification mirrors
`firmware/esp32cam/README.md` steps.
