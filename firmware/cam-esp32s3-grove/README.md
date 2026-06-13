# cam-esp32s3-grove — Hearth Standard cam

Reliable on-device person detection using a **Seeed XIAO ESP32-S3** as the Wi-Fi
host and a **Seeed Grove Vision AI Module V2** (dedicated vision SoC) running
**YOLOv8 person detection**. The Grove module owns the camera and the model; the
XIAO reads the top person box + JPEG preview over **I2C (SSCMA)**, records a clip
to SD, and publishes the FABRIC.md §4 `CameraEvent`. Announces
`person_detect:"reliable"`. Reuses `firmware/common`.

## Hardware
- Seeed Studio **XIAO ESP32-S3** + a microSD breakout/expansion (FAT32 card).
- **Grove Vision AI Module V2**, connected to the XIAO's Grove I2C
  (SDA=GPIO5, SCL=GPIO6 by default; addr `0x62`).
- The Grove module must be flashed with a **person / COCO YOLOv8** model via Seeed
  SenseCraft (class 0 = person).

## Model / SSCMA
- **Default build:** `Seeed_Arduino_SSCMA` is not linked and `HEARTH_HAVE_SSCMA`
  is undefined, so `GroveVision` is a documented stub (no detections) — the fabric
  plumbing still builds and runs. See `grove_vision.cpp` `TODO(model)`.
- **Enable detection:** uncomment `seeed-studio/Seeed_Arduino_SSCMA` in
  `lib_deps`, add `-DHEARTH_HAVE_SSCMA` to `build_flags`, rebuild. The wrapper then
  reads real YOLOv8 boxes/scores.

## Configure
```
cp config.example.h config.h    # done in-repo
```
Set name/location, `HEARTH_MQTT_HOST`, threshold (default 0.70, higher than Budget).

## Build / flash
```
pip install platformio
pio run                # compile
pio run -t upload      # flash
pio device monitor
```
Board `seeed_xiao_esp32s3`, partitions `partitions.csv` (dual-app OTA).

## Pairing & endpoints
Identical to the Budget cam: SoftAP `Hearth-Setup-<hex6>` → `POST /provision` →
`hearth-cam-<hex6>.local`. HTTP API (port 80): `/ /status /snapshot /stream
/clips /clips/<f> /provision /pair /config`; media + clips require the HMAC bearer
(FABRIC.md §6). `/snapshot` and `/stream` serve the Grove module's JPEG preview.
