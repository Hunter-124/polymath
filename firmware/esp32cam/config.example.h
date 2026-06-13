#pragma once
// esp32cam (legacy AI-Thinker) — per-device config. Copy to config.h.
// Wi-Fi creds are now provisioned at runtime via SoftAP (FABRIC.md §6), so they
// are NOT here. Only labels + hub address.

#define HEARTH_DEVICE_NAME  "Side Yard"
#define HEARTH_LOCATION     "Exterior"

#define HEARTH_MQTT_HOST    "hearth-hub.local"
#define HEARTH_MQTT_PORT    1883

// Motion-only on this tier (no person model). Retention for SD clips.
#define HEARTH_RETENTION_DAYS  7

// Motion sensitivity: fraction of coarse cells that must change (0..1).
#define HEARTH_MOTION_FRAC     0.05f
