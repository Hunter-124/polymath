# Hearth Hardware — SKU / BOM ladder (2026)

Hearth ships as a hub **and** a tiered line of edge devices that are useful standalone. The design
target for cameras: **on-device person detection, people-only clips written to the device's own SD card,
a standalone Wi-Fi server, and net-0 hosting** (no cloud, no subscription) on the cheap tiers. Firmware
for every device lives under [`firmware/`](../firmware/) and speaks the [device fabric contract](FABRIC.md).

Prices are 2026 street estimates and move around (notably Raspberry Pi); lock supplier pricing before a
BOM commit.

---

## 1. Cameras

| Tier | Hardware | ~Unit | On-device person detect | SD | Standalone Wi-Fi server | Firmware |
|---|---|---|---|---|---|---|
| **Budget** | Seeed XIAO ESP32-S3 Sense (OV2640, 8 MB PSRAM) | ~$14 | **trigger-grade** — FOMO/Swift-YOLO ~7 FPS (conservative thresholds; optional hub/phone confirm) | microSD | ✅ | [`firmware/cam-esp32s3`](../firmware/cam-esp32s3) |
| **Standard** | XIAO ESP32-S3 (Wi-Fi host) **+ Seeed Grove Vision AI Module V2** (Himax WiseEye2, Ethos-U55 NPU) | ~$30 | **reliable** — YOLOv8 13–21 FPS @0.35 W | microSD | ✅ | [`firmware/cam-esp32s3-grove`](../firmware/cam-esp32s3-grove) |
| **Pro** | Canaan CanMV-K230 (dual RISC-V + 6-TOPS KPU) | ~$30–50 | **reliable** — YOLOv5s ~38 FPS, 4K, SD ≤ 1 TB | microSD | ✅ (verify on-board Wi-Fi variant) | [`firmware/cam-k230`](../firmware/cam-k230) |
| **Flagship** | Raspberry Pi 5 + AI HAT+ (Hailo-8L, 13 TOPS) | ~$200+ | **reliable** — YOLOv8n ~60 FPS, multi-stream NVR | NVMe/SD | ✅ (full Linux) | [`firmware/cam-pi`](../firmware/cam-pi) |
| Legacy | AI-Thinker ESP32-CAM (OV2640) | ~$7 | motion-only (+ optional phone-side ML) | microSD | ✅ | [`firmware/esp32cam`](../firmware/esp32cam) |

Notes & gotchas:
- **ESP32-P4 camera boards were dropped** — the P4 has no native Wi-Fi (needs an ESP32-C6 companion), so it
  can't be a one-chip standalone server.
- Grove Vision AI V2 / Sony IMX500 / Luxonis OAK are **inference engines, not servers** — always pair them
  with a host MCU/Pi (the Standard tier does exactly this).
- The Flagship Pi can also run in `--edge-hub` mode and do detection on behalf of Budget cameras on the LAN.

Sources: XIAO ESP32-S3 Sense ([seeedstudio](https://www.seeedstudio.com/XIAO-ESP32S3-Sense-p-5639.html)),
Grove Vision AI V2 ([cnx-software](https://www.cnx-software.com/2024/01/19/16-grove-vision-ai-v2-module-features-wiseeye2-hx6538-arm-cortex-m55-ethos-u55-ai-microcontroller/)),
CanMV-K230 ([cnx-software](https://www.cnx-software.com/2024/11/18/29-banana-pi-bpi-canmv-k230d-zero-features-kendryte-k230d-risc-v-soc-for-aiot-applications/)),
Pi AI HAT+ Hailo-8L ([raspberrypi.com](https://www.raspberrypi.com/news/raspberry-pi-ai-hat/)).

---

## 2. Voice satellites

Always-listening far-field units: on-device wake word (microWakeWord), then stream **16 kHz mono audio
over Wi-Fi/UDP** to the hub (Opus or PCM16); play TTS back. **Wi-Fi, not BLE** (BLE conflicts with the
audio pipeline and is bandwidth-marginal for duplex voice). Firmware: [`firmware/voice-sat`](../firmware/voice-sat).

| Tier | Hardware | ~Unit | Notes |
|---|---|---|---|
| **Bundled-with-camera** | ESP32-S3 + ICS-43434 I²S mic + MAX98357A amp | ~$8 BOM | piggybacks on a camera's power/MCU class; near/mid-field |
| **Room satellite** | Seeed ReSpeaker Lite (XMOS XU316 AEC/DSP) | ~$37 | real far-field; or turnkey HA Voice PE (~$59) |
| **Premium far-field** | ReSpeaker XVF3800 4-mic + XIAO ESP32-S3 | ~$55 | 360° beamforming, ~5 m, direction-of-arrival |

The hub already runs whisper.cpp ASR + Piper TTS + openWakeWord (backstop), so satellites only capture +
wake-gate + stream. Sources: HA Voice PE ([home-assistant.io](https://www.home-assistant.io/voice-pe/)),
ReSpeaker Lite / XVF3800 ([seeedstudio](https://www.seeedstudio.com/ReSpeaker-Lite-Voice-Assistant-Kit-p-5929.html)),
microWakeWord ([github](https://github.com/kahrendt/esphome-on-device-wake-word)).

---

## 3. Lab instrument modules — "Hearth Measurement Module" (HMM)

One **ESP32-S3** front-end + a **STEMMA-QT / Qwiic** I²C daisy-chain of hot-swappable sensor "tiles."
Each tile maps to one instrument id and publishes timestamped readings (HA-discovery-style fields) over
the fabric. Firmware: [`firmware/hmm`](../firmware/hmm).

| Domain | Recommended part | Bus | Notes |
|---|---|---|---|
| Mass / force | **NAU7802** 24-bit + load cell | I²C | beats HX711 (I²C, quieter, on-die temp comp) |
| Temperature (RTD) | **MAX31865** (PT100/PT1000) | SPI | lab-grade stability |
| Temperature (thermocouple) | **MAX31856** (K/J/T/E/N/R/S/B) | SPI | wide range, good cold-junction |
| Temperature/RH (precise) | **SHT45** / **TMP117** | I²C | ±0.1 °C |
| CO₂ | **SCD41** (NDIR) | I²C | true CO₂, not eCO₂ |
| Pressure | **BMP390** | I²C/SPI | ±0.03 hPa rel. |
| Voltage / current | **INA228** (or INA226) | I²C | per-instrument power logging |
| Spectral / colorimetry | **AS7341** 11-channel | I²C | absorbance/OD with a fixed LED |
| pH | **Atlas EZO-pH** + lab probe | I²C/UART | opto-isolated, on-board calibration |

**Reporting:** the canonical reading shape is `{unique_id, value, unit, device_class, ts}` (mirrors Home
Assistant MQTT-discovery so ESPHome modules map 1:1). The hub clock is authoritative; a per-module DS3231
only provides offline holdover. Real benchtop instruments that already speak SCPI/VXI-11 are bridged in
separately rather than forced onto this scheme.

Sources: NAU7802 ([Adafruit](https://learn.adafruit.com/adafruit-nau7802-24-bit-adc-stemma-qt-qwiic/overview)),
MAX31865 ([Adafruit](https://www.adafruit.com/product/3328)), SHT45 ([Adafruit](https://www.adafruit.com/product/5665)),
SCD41 ([Sensirion](https://sensirion.com/products/catalog/SCD41)), Atlas EZO-pH ([atlas-scientific](https://atlas-scientific.com/embedded-solutions/ezo-ph-circuit/)).

---

## 4. Wall panels

Two delivery models: **native Qt6 UI** on capable Linux SBCs, or the **React PWA** on everything else.
ESP32 displays are voice/quick-control satellites only — too weak for the dashboard. See [`PANELS.md`](PANELS.md).

| Tier | Hardware | ~All-in | Runs |
|---|---|---|---|
| **Budget** | Refurb Fire HD 10 + Fully Kiosk | ~$60–75 | React PWA |
| **Standard** | Raspberry Pi 5 (4 GB) + Pi Touch Display 2 (7″) | ~$160–200 | **native Qt6 `Hearth.exe --panel`** |
| **Premium** | PoE in-wall Android panel (RK3566, Android 11) | ~$180–260 | React PWA, single-cable PoE |

Because two of three SKUs are PWA-only, the **React app is the critical-path surface** and is kept
feature-equal to the hub. Sources: Pi Touch Display 2 ([raspberrypi.com](https://www.raspberrypi.com/products/touch-display-2/)),
Boot2Qt on Pi ([doc.qt.io](https://doc.qt.io/Boot2Qt/b2qt-qsg-raspberry.html)), Fully Kiosk ([home-assistant.io](https://www.home-assistant.io/integrations/fully_kiosk/)).
