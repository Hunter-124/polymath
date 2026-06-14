#include "tiles.h"
#include <Wire.h>

// Each tile compiles only if its HEARTH_TILE_* macro is defined (config.h) AND the
// matching driver lib is in platformio.ini lib_deps. The #include lives inside the
// guard so disabled tiles don't force their lib to be present.

namespace hearth {

static InstrumentDesc mk(const String& deviceId, int ch, const char* name,
                         const char* unit, const char* klass,
                         double lo, double hi) {
    InstrumentDesc d;
    d.id = deviceId + "_" + klass + "_" + unit;   // FABRIC.md §3 id convention
    d.name = name; d.channel = ch; d.unit = unit; d.deviceClass = klass;
    d.expectedMin = lo; d.expectedMax = hi;
    return d;
}

// ─── NAU7802 (mass) ──────────────────────────────────────────────────────────
#ifdef HEARTH_TILE_NAU7802
#include <SparkFun_Qwiic_Scale_NAU7802_Arduino_Library.h>
static NAU7802 s_nau;
bool TileNAU7802::begin() { return s_nau.begin(); }
bool TileNAU7802::read(double& out) {
    if (!s_nau.available()) return false;
    out = s_nau.getReading() * cal_ / 1000.0;   // raw counts -> grams via calFactor
    return true;
}
InstrumentDesc TileNAU7802::describe(const String& id) const {
    return mk(id, 0, "Balance", "g", "mass", 0, 500);
}
#endif

// ─── MAX31865 (RTD temperature) ──────────────────────────────────────────────
#ifdef HEARTH_TILE_MAX31865
#include <Adafruit_MAX31865.h>
static Adafruit_MAX31865 s_rtd(10);            // CS on GPIO10 (SPI)
bool TileMAX31865::begin() { return s_rtd.begin(MAX31865_3WIRE); }
bool TileMAX31865::read(double& out) {
    out = s_rtd.temperature(100.0, 430.0);     // PT100, 430R ref
    return !s_rtd.readFault();
}
InstrumentDesc TileMAX31865::describe(const String& id) const {
    return mk(id, 0, "RTD Temp", "C", "temperature", -50, 300);
}
#endif

// ─── MAX31856 (thermocouple) ─────────────────────────────────────────────────
#ifdef HEARTH_TILE_MAX31856
#include <Adafruit_MAX31856.h>
static Adafruit_MAX31856 s_tc(11);             // CS on GPIO11 (SPI)
bool TileMAX31856::begin() { if (!s_tc.begin()) return false; s_tc.setThermocoupleType(MAX31856_TCTYPE_K); return true; }
bool TileMAX31856::read(double& out) { out = s_tc.readThermocoupleTemperature(); return !s_tc.readFault(); }
InstrumentDesc TileMAX31856::describe(const String& id) const {
    return mk(id, 0, "Thermocouple", "C", "temperature", -200, 1350);
}
#endif

// ─── SHT45 (temperature / RH) ────────────────────────────────────────────────
#ifdef HEARTH_TILE_SHT45
#include <Adafruit_SHT4x.h>
static Adafruit_SHT4x s_sht;
bool TileSHT45::begin() { return s_sht.begin(); }
bool TileSHT45::read(double& out) {
    sensors_event_t h, t; if (!s_sht.getEvent(&h, &t)) return false;
    out = t.temperature; return true;          // publishes temperature channel
}
InstrumentDesc TileSHT45::describe(const String& id) const {
    // NOTE: one tile == one instrument id. SHT45 also exposes RH; add a second
    // tile instance/class if you want a separate humidity instrument.
    return mk(id, 0, "Air Temp", "C", "temperature", -40, 125);
}
#endif

// ─── SCD41 (CO2) ─────────────────────────────────────────────────────────────
#ifdef HEARTH_TILE_SCD41
#include <SensirionI2CScd4x.h>
static SensirionI2CScd4x s_scd;
bool TileSCD41::begin() { s_scd.begin(Wire); s_scd.startPeriodicMeasurement(); return true; }
bool TileSCD41::read(double& out) {
    uint16_t co2; float t, rh; bool ready = false;
    if (s_scd.getDataReadyFlag(ready) || !ready) return false;
    if (s_scd.readMeasurement(co2, t, rh) || co2 == 0) return false;
    out = co2; return true;
}
InstrumentDesc TileSCD41::describe(const String& id) const {
    return mk(id, 0, "CO2", "ppm", "carbon_dioxide", 400, 5000);
}
#endif

// ─── BMP390 (pressure) ───────────────────────────────────────────────────────
#ifdef HEARTH_TILE_BMP390
#include <Adafruit_BMP3XX.h>
static Adafruit_BMP3XX s_bmp;
bool TileBMP390::begin() { return s_bmp.begin_I2C(); }
bool TileBMP390::read(double& out) { if (!s_bmp.performReading()) return false; out = s_bmp.pressure; return true; }
InstrumentDesc TileBMP390::describe(const String& id) const {
    return mk(id, 0, "Pressure", "Pa", "pressure", 30000, 110000);
}
#endif

// ─── INA228 (voltage) ────────────────────────────────────────────────────────
#ifdef HEARTH_TILE_INA228
#include <Adafruit_INA228.h>
static Adafruit_INA228 s_ina;
bool TileINA228::begin() { return s_ina.begin(); }
bool TileINA228::read(double& out) { out = s_ina.readBusVoltage() / 1000.0; return true; }  // mV->V
InstrumentDesc TileINA228::describe(const String& id) const {
    return mk(id, 0, "Bus Voltage", "V", "voltage", 0, 36);
}
#endif

// ─── AS7341 (spectral) ───────────────────────────────────────────────────────
#ifdef HEARTH_TILE_AS7341
#include <Adafruit_AS7341.h>
static Adafruit_AS7341 s_as;
bool TileAS7341::begin() { return s_as.begin(); }
bool TileAS7341::read(double& out) {
    if (!s_as.readAllChannels()) return false;
    out = s_as.getChannel(AS7341_CHANNEL_CLEAR);   // representative clear-channel count
    return true;
}
InstrumentDesc TileAS7341::describe(const String& id) const {
    return mk(id, 0, "Spectral (clear)", "", "illuminance", 0, 65535);
}
#endif

// ─── Atlas EZO-pH (I2C) ──────────────────────────────────────────────────────
#ifdef HEARTH_TILE_EZO_PH
// Atlas EZO devices use a simple ASCII I2C protocol: write "R\0", wait ~900ms,
// then read a status byte + ASCII float. No vendor lib needed.
bool TileEzoPh::begin() {
    Wire.beginTransmission(kAddr);
    return Wire.endTransmission() == 0;
}
bool TileEzoPh::read(double& out) {
    Wire.beginTransmission(kAddr); Wire.write('R'); Wire.endTransmission();
    delay(900);
    Wire.requestFrom((int)kAddr, 20);
    if (!Wire.available()) return false;
    int code = Wire.read();                 // 1 = success
    char buf[20]; int i = 0;
    while (Wire.available() && i < 19) { char c = Wire.read(); if (!c) break; buf[i++] = c; }
    buf[i] = 0;
    if (code != 1) return false;
    out = atof(buf); return out > 0;
}
InstrumentDesc TileEzoPh::describe(const String& id) const {
    return mk(id, 0, "pH", "pH", "ph", 0, 14);
}
#endif

} // namespace hearth
