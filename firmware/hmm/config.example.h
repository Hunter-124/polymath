#pragma once
// hmm (Hearth Measurement Module, ESP32-S3) — per-device config. Copy to config.h.

#define HEARTH_DEVICE_NAME  "Bench HMM"
#define HEARTH_LOCATION     "Lab"

#define HEARTH_MQTT_HOST    "hearth-hub.local"
#define HEARTH_MQTT_PORT    1883

// Qwiic/STEMMA-QT I2C pins (XIAO/ESP32-S3 default Qwiic header).
#define HEARTH_I2C_SDA  5
#define HEARTH_I2C_SCL  6

// Publish cadence (seconds) per instrument.
#define HEARTH_READ_PERIOD_S  5

// --- Enable tiles --------------------------------------------------------------
// Uncomment one or more. Each enabled tile must also have its driver lib in
// platformio.ini lib_deps (the relevant lines are listed there per tile).
#define HEARTH_TILE_SHT45     1     // temp/RH  (Adafruit SHT4x) — on by default
#define HEARTH_TILE_SCD41     1     // CO2      (Sensirion SCD4x)
// #define HEARTH_TILE_NAU7802  1   // mass     (SparkFun Qwiic Scale)
// #define HEARTH_TILE_MAX31865 1   // RTD temp (Adafruit MAX31865, SPI)
// #define HEARTH_TILE_MAX31856 1   // thermocouple (Adafruit MAX31856, SPI)
// #define HEARTH_TILE_BMP390   1   // pressure (Adafruit BMP3XX)
// #define HEARTH_TILE_INA228   1   // V/I      (Adafruit INA228)
// #define HEARTH_TILE_AS7341   1   // spectral (Adafruit AS7341)
// #define HEARTH_TILE_EZO_PH   1   // pH       (Atlas EZO-pH, no extra lib)
