# esp32cam — Hearth Legacy cam (AI-Thinker ESP32-CAM)

The original dumb-MJPEG firmware, **upgraded** to a first-class (if motion-only)
Hearth fabric device. It keeps the MJPEG stream and adds: stable `device_id`,
SoftAP provisioning, MQTT announce/presence/event, SD clip-on-motion, OTA, and the
full device HTTP API — reusing `firmware/common`.

> **Detection tier:** AI-Thinker has no AI accelerator, so detection here is
> **motion-only**. Events publish `kind:"motion"` and announce
> `person_detect:"trigger"`. For reliable person detection use the **cam-esp32s3**
> (Budget) or **cam-esp32s3-grove** (Standard) tiers.

The pre-upgrade sketch is kept for reference as
[`legacy_mjpeg.ino.reference`](legacy_mjpeg.ino.reference).

## Hardware
- AI-Thinker **ESP32-CAM** (OV2640 + PSRAM), 4 MB flash.
- USB-to-TTL (FTDI) adapter **or** the ESP32-CAM-MB programmer shield.
- Optional microSD (FAT32) for clips. SD uses **SD_MMC 1-bit** (shares the
  GPIO4 flash-LED line). With no card, the device still streams + emits motion
  events, just without a `clip_url`.

## Configure
```
cp config.example.h config.h    # done in-repo
```
Edit `config.h`: device name/location, `HEARTH_MQTT_HOST`, motion sensitivity.
Wi-Fi creds are **no longer compiled in** — they are set at runtime via SoftAP
provisioning (FABRIC.md §6).

## Build / flash (PlatformIO)
```
pip install platformio
pio run                              # compile
# wire FTDI, jumper GPIO0->GND, press RST, then:
pio run -t upload
# remove the GPIO0 jumper, press RST
pio device monitor
```
Board `esp32cam`, partitions `partitions.csv` (4 MB dual-app OTA).

**Arduino IDE users:** copy `firmware/common/*` into a libraries folder (or use the
PlatformIO flow above). Board = `AI Thinker ESP32-CAM`; the bundled
`legacy_mjpeg.ino.reference` is the old, dependency-free MJPEG-only sketch if you
just want the original behaviour.

## First-boot pairing
1. First boot → AP **`Hearth-Setup-<hex6>`**; join it, open `http://192.168.4.1/`,
   submit SSID/password (`POST /provision`). Device persists + reboots into STA.
2. Reachable at `http://hearth-cam-<hex6>.local/`. The status page renders the
   pairing QR JSON; scan it in the Hearth app (FABRIC.md §6).

## Endpoints (port 80)
`GET /` `GET /status` · `GET /snapshot`* · `GET /stream`* (MJPEG, the URL to
register) · `GET /clips`* · `GET /clips/<f>`* · `POST /provision` · `POST /pair` ·
`GET /config` · `POST /config`*  — *require the HMAC bearer.

## Power tips
Use a solid 5V/2A supply; the camera draws spikes weak USB power can't tolerate
(brown-outs garble the image). Keep these on a trusted LAN/VLAN.
