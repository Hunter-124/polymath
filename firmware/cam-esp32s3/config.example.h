#pragma once
// cam-esp32s3 (Budget cam, Seeed XIAO ESP32-S3 Sense) — per-device config.
// Copy to config.h and edit. Secrets live here; config.h is .gitignore'd.

#define HEARTH_DEVICE_NAME  "Front Door"     // human label (announce/status)
#define HEARTH_LOCATION     "Entry"          // room/area label

// MQTT hub. The device still works fully with NO hub (HTTP + direct pair).
#define HEARTH_MQTT_HOST    "hearth-hub.local"
#define HEARTH_MQTT_PORT    1883

// Edge detection defaults (overridable live via cmd/config + POST /config).
#define HEARTH_PERSON_THRESHOLD  0.60f       // conservative
#define HEARTH_RETENTION_DAYS    14

// Define to compile against a real ESP-DL person model (see person_detector.cpp
// TODO(model)). Leave undefined to ship the motion-fallback build.
// #define HEARTH_HAVE_ESPDL 1
