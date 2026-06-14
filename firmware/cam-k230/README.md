# cam-k230 — Hearth Pro cam (Canaan CanMV-K230)

The flagship-but-embedded camera tier: **Canaan CanMV-K230** (dual RISC-V + KPU
NPU) running **CanMV MicroPython**. KPU YOLO person detection → clip-on-person to
microSD → FABRIC.md §4 `CameraEvent` over MQTT, plus the device HTTP API (§6) and
SoftAP provisioning. Same wire contract as the ESP tiers.

## Why MicroPython
CanMV (Canaan's MicroPython distribution for the K230) is the norm for this board:
it ships the `sensor`, `image`, KPU (`nncase`) and `network`/`socket` modules.
There is no PlatformIO target — you copy the `.py` files to the board's flash/SD.

## Hardware
- **Canaan CanMV-K230** dev board with a compatible MIPI/DVP camera module.
- microSD (FAT32) for the KPU model + clips.
- Wi-Fi (onboard or a supported USB/SDIO module per your CanMV image).

## Files (copy all to the board)
| File | Role |
|---|---|
| `main.py` | entry: camera → detect → clip → event loop + provisioning bootstrap |
| `config.py` | secrets/labels (copy from `config.example.py`) |
| `detector.py` | KPU YOLO person detector + motion fallback |
| `hearth_id.py` | `device_id = hearth-cam-<hex6>` |
| `hearth_auth.py` | HMAC bearer + pairing QR (§6) |
| `hearth_net.py` | STA + SoftAP provisioning (`/sdcard/hearth/wifi.json`) |
| `hearth_mqtt.py` | birth/LWT, announce, event, cmd dispatch (umqtt.simple) |
| `hearth_httpd.py` | device HTTP API (threaded socket server) |

## Model
Put a person/COCO YOLO **`.kmodel`** at `MODEL_PATH` (default
`/sdcard/models/person_yolov8n.kmodel`), exported for the K230 KPU via Canaan's
nncase toolchain.
- **If present + wired:** announces `person_detect:"reliable"`, events
  `kind:"person"`. The KPU load/parse is marked `TODO(model)` in `detector.py`
  (the exact `nncase_runtime` API varies by CanMV image) — fill it for your image.
- **If absent / not yet wired:** falls back to a frame-difference **motion gate**;
  events become `kind:"motion"` and it announces `person_detect:"trigger"`.

## Install / run
```
# 1. Flash a recent CanMV image to the K230.
# 2. Copy this directory's .py files to the board (CanMV IDE, ampy, or /sdcard).
cp config.example.py config.py     # then edit DEVICE_NAME / MQTT_HOST
# 3. First boot with no creds -> AP "Hearth-Setup-<hex6>"; join, open
#    http://<ap-ip>/, submit Wi-Fi. It saves /sdcard/hearth/wifi.json and reboots.
# 4. Reachable at http://hearth-cam-<hex6>.local/ (status page shows the pair QR).
```
`main.py` runs on boot (CanMV auto-runs `main.py`).

## Verify without hardware
`python -m py_compile *.py` passes on host (CanMV-only modules are import-guarded).
The MQTT/HTTP payloads match FABRIC.md and can be checked against the hub broker.

## Notes / FABRIC
- Clips are written as `<unix>.mjpeg` for parity with the ESP tiers and served as
  `video/mp4` (opaque URL). A later build can mux real MP4 via the CanMV encoder.
- `cmd/ota` on the K230 is a stub (`TODO(ota)`) — MicroPython OTA means swapping
  `.py` files / a CanMV image update, not an ESP `Update.flash`.
