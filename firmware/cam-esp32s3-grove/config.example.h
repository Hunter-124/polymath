#pragma once
// cam-esp32s3-grove (Standard cam) — XIAO ESP32-S3 host + Grove Vision AI V2.
// Copy to config.h and edit.

#define HEARTH_DEVICE_NAME  "Garage"
#define HEARTH_LOCATION     "Garage"

#define HEARTH_MQTT_HOST    "hearth-hub.local"
#define HEARTH_MQTT_PORT    1883

// Higher-confidence "reliable" person filter (Grove runs YOLOv8 on its own SoC).
#define HEARTH_PERSON_THRESHOLD  0.70f
#define HEARTH_RETENTION_DAYS    14

// Grove Vision AI Module V2 I2C address (SSCMA default).
#define HEARTH_GROVE_I2C_ADDR    0x62

// I2C pins on the XIAO ESP32-S3 Grove header.
#define HEARTH_I2C_SDA  5
#define HEARTH_I2C_SCL  6
