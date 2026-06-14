#pragma once
// voice-sat (ESP32-S3 voice satellite) — per-device config. Copy to config.h.

#define HEARTH_DEVICE_NAME  "Kitchen Sat"
#define HEARTH_LOCATION     "Kitchen"

#define HEARTH_MQTT_HOST    "hearth-hub.local"
#define HEARTH_MQTT_PORT    1883

// Audio plane (FABRIC.md §8): UDP to hub:8770, 16 kHz mono.
#define HEARTH_HUB_AUDIO_HOST  "hearth-hub.local"
#define HEARTH_HUB_AUDIO_PORT  8770
#define HEARTH_ROOM_ID         0          // uint8 room_id in the datagram header

// Codec: 0=PCM16 (fallback, always available), 1=Opus (needs libopus, see below).
#define HEARTH_CODEC_OPUS  0

// Wake phrase label reported on hearth/<id>/wake.
#define HEARTH_WAKE_PHRASE  "hey hearth"

// --- Hardware variant -------------------------------------------------------
// Exactly one is selected by a -D flag in platformio.ini (see [env] sections):
//   HEARTH_HW_BUNDLED_MIC  ICS-43434 I2S mic + MAX98357A amp (default board)
//   HEARTH_HW_RESPEAKER    Seeed ReSpeaker Lite
//   HEARTH_HW_XVF3800      XMOS XVF3800 dev kit
// Pins per variant live in audio_pins.h.
