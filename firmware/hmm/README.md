# hmm — Hearth Measurement Module

An ESP32-S3 instrument hub: a clean `SensorTile` driver interface plus concrete
tiles for Qwiic/STEMMA-QT (and a couple of SPI) lab sensors. **Each enabled tile
maps to one Hearth instrument `id`**, announces itself (FABRIC.md §3), and
publishes a retained `Reading` (§5) on a timer. Reuses `firmware/common`.

## Tiles
| Tile | Sensor | unit / device_class | bus | driver lib |
|---|---|---|---|---|
| NAU7802 | load cell / balance | `g` / mass | I2C | SparkFun Qwiic Scale |
| MAX31865 | PT100 RTD | `C` / temperature | SPI | Adafruit MAX31865 |
| MAX31856 | thermocouple (K) | `C` / temperature | SPI | Adafruit MAX31856 |
| SHT45 | air temp/RH | `C` / temperature | I2C | Adafruit SHT4x |
| SCD41 | CO2 | `ppm` / carbon_dioxide | I2C | Sensirion SCD4x |
| BMP390 | pressure | `Pa` / pressure | I2C | Adafruit BMP3XX |
| INA228 | bus voltage | `V` / voltage | I2C | Adafruit INA228 |
| AS7341 | spectral (clear) | counts / illuminance | I2C | Adafruit AS7341 |
| EZO-pH | Atlas pH | `pH` / ph | I2C | none (raw I2C) |

`instrument_id` = `<device_id>_<class>_<unit>`, e.g. `hearth-hmm-44ccbb_mass_g`
(matches FABRIC.md §3). The payload shape is HA-MQTT-discovery compatible (`unit`,
`device_class`).

## Enable tiles
Edit `config.h` — uncomment the `HEARTH_TILE_*` you have, **and** uncomment the
matching `lib_deps` line in `platformio.ini`. The default build ships **SHT45 +
SCD41** enabled (and their libs uncommented), so `pio run` works out of the box.
Tiles whose sensor isn't detected at boot are dropped automatically (`begin()`
returns false) — the module just publishes the ones that are present.

Add a new sensor by writing a `SensorTile` subclass in `tiles.{h,cpp}` behind a
`HEARTH_TILE_*` guard; nothing else changes.

## Build / flash
```
pip install platformio
pio run                 # compiles with SHT45 + SCD41 by default
pio run -t upload
pio device monitor
```
Board `seeed_xiao_esp32s3` (any ESP32-S3 with a Qwiic header works; adjust
`HEARTH_I2C_SDA/SCL`). Partitions `partitions.csv` (dual-app OTA).

## Pairing
SoftAP `Hearth-Setup-<hex6>` → `POST /provision` → joins Wi-Fi; `device_id` is
`hearth-hmm-<hex6>`. Readings appear in the hub as `measurements` with `in_range`
computed against each tile's `expected_min/max`.

## SHT45 note
SHT45 reports both temperature and RH. One tile == one instrument id, so this tile
publishes the temperature channel; add a second small tile class for a separate
humidity instrument if you want both as distinct instruments.
