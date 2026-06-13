# Hearth firmware

Edge-device firmware for the **Hearth** smart-home/lab product. Every device — a
camera, a voice satellite, or a lab instrument — is an autonomous node on the
**Hearth fabric**. All firmware here implements the frozen wire contract in
[`docs/FABRIC.md`](../docs/FABRIC.md).

## How the fabric works (one paragraph)
Devices speak two planes. **Control/telemetry** is small JSON over **MQTT** to the
hub's embedded broker (`:1883`): retained presence with an MQTT Last-Will, a boot
**announce** of capabilities, person/motion **events**, instrument **readings**, and
hub→device **commands** (`config`/`tts`/`identify`/`ota`). **Media/bulk** is pulled
from each device over its own **HTTP** server (snapshot, MJPEG, recorded clips) and
live satellite audio is **Opus/PCM over UDP** to `hub:8770`. A device that never
sees a hub is still fully usable: it serves its own HTTP API and pairs **directly**
with the mobile app over a SoftAP captive flow, authenticating every request with
`Authorization: Bearer HMAC-SHA256(key, path+ts)` using a per-device key carried in
its pairing QR. Clips stay on the device's SD card (the hub keeps only a thumbnail
+ URL) — the "net-0 storage" guarantee. See `docs/FABRIC.md` for the byte-level
JSON and topics.

## Projects (SKU / tier table)

| Dir | SKU / tier | Hardware | Detection | Builds with |
|---|---|---|---|---|
| [`esp32cam/`](esp32cam/) | Legacy cam | AI-Thinker ESP32-CAM | motion-only (`trigger`) | PlatformIO |
| [`cam-esp32s3/`](cam-esp32s3/) | **Budget** cam | XIAO ESP32-S3 Sense | on-device ESP-DL person (`reliable`*) | PlatformIO |
| [`cam-esp32s3-grove/`](cam-esp32s3-grove/) | **Standard** cam | XIAO ESP32-S3 + Grove Vision AI V2 | YOLOv8 person, high-conf (`reliable`*) | PlatformIO |
| [`cam-k230/`](cam-k230/) | **Pro** cam | Canaan CanMV-K230 (KPU) | KPU YOLO person (`reliable`*) | CanMV MicroPython |
| [`cam-pi/`](cam-pi/) | **Flagship** cam / edge-hub | Raspberry Pi 5 (+Hailo) | ONNX/Hailo YOLO + local NVR (`reliable`*) | CMake (Linux) |
| [`voice-sat/`](voice-sat/) | Voice satellite | ESP32-S3 + I2S mic/amp | microWakeWord → UDP audio | PlatformIO |
| [`hmm/`](hmm/) | Measurement module | ESP32-S3 + Qwiic sensors | n/a (instrument tiles) | PlatformIO |
| [`common/`](common/) | shared library | all ESP32/S3 projects | — | (linked by the above) |

\* The ML model / proprietary SDK blob isn't fetched in this environment, so each
of these ships a **clean detector interface + documented stub + working
motion/heuristic fallback** (marked `TODO(model)`). Builds succeed today; the
device degrades to motion-only and announces `person_detect:"trigger"` until the
model is wired in, then it reports `"reliable"`. See each project's README.

## Build quickstart
- **PlatformIO projects** (`esp32cam`, `cam-esp32s3`, `cam-esp32s3-grove`,
  `voice-sat`, `hmm`): `pip install platformio` then `pio run` in the project dir.
  They pull in `firmware/common` via a `symlink://../common` lib dependency.
- **cam-k230** (MicroPython): copy the `.py` files to a CanMV-K230. Syntax-check on
  a host with `python -m py_compile *.py`.
- **cam-pi** (C++): `cmake -S . -B build && cmake --build build` on a Linux/Pi host
  with `libssl-dev` (and optionally `libmosquitto-dev` / ONNX Runtime).

## Config & secrets
Each project has a `config.example.*`. A `config.h`/`config.py`/`cam-pi.json` is
**gitignored** (`firmware/.gitignore`); copy the example and edit. The in-repo
defaults use placeholders only — **Wi-Fi credentials are never compiled in**, they
are set at runtime via the SoftAP provisioning captive page (FABRIC.md §6).

## FABRIC.md ambiguities encountered
1. **Clip container.** §4's `clip_url` example ends in `.mp4`, but the field is an
   opaque URL and MP4 muxing is impractical on the MCU tiers. ESP/K230 record
   `<unix>.mjpeg`; cam-pi writes `<unix>.mp4` (placeholder until the muxer is wired).
   The hub treats the URL opaquely either way.
2. **`/pair` semantics.** §6 says `/pair` "exchanges QR key → device token", but the
   app caches `{device_id, key}` and self-derives per-request HMAC bearers, so there
   is no separate server session to mint. Our `/pair` validates the key and echoes
   `HMAC(key, "/pair"+ts)` as confirmation.
3. **HMAC `ts` transport.** §6 specifies `HMAC(key, path+ts)` but not how `ts`
   reaches the device. We send it in an `X-Hearth-Ts` header and HMAC over
   `path + "." + ts`, with a ±120 s freshness window (skipped while the device clock
   is unsynced). All tiers (ESP/K230/Pi) implement this identically.
