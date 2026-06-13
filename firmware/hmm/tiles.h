#pragma once
// Concrete sensor tiles for the Hearth Measurement Module. Each maps to one
// instrument id. Tiles are thin wrappers over mainstream Adafruit/SparkFun
// drivers (added in platformio.ini lib_deps and gated by the per-tile HEARTH_TILE_*
// macro from config.h). When a tile's macro/lib is absent the tile is simply not
// instantiated (see hmm.cpp) — the project still builds with whatever subset is on.
//
//   NAU7802  mass (g)            INA228   voltage/current (V, A)
//   MAX31865 RTD temp (C)        AS7341   spectral (a representative channel)
//   MAX31856 thermocouple (C)    EZO-pH   pH (Atlas Scientific, I2C)
//   SHT45    temp/RH (C, %)
//   SCD41    CO2 (ppm)
//   BMP390   pressure (Pa)

#include "sensor_tile.h"

namespace hearth {

#ifdef HEARTH_TILE_NAU7802
class TileNAU7802 : public SensorTile {       // mass / load cell
public:
    explicit TileNAU7802(float calFactor = 1.0f) : cal_(calFactor) {}
    bool begin() override; bool read(double& out) override;
    InstrumentDesc describe(const String& id) const override;
    const char* driver() const override { return "NAU7802"; }
private: float cal_;
};
#endif

#ifdef HEARTH_TILE_MAX31865
class TileMAX31865 : public SensorTile {      // RTD temperature
public:
    bool begin() override; bool read(double& out) override;
    InstrumentDesc describe(const String& id) const override;
    const char* driver() const override { return "MAX31865"; }
};
#endif

#ifdef HEARTH_TILE_MAX31856
class TileMAX31856 : public SensorTile {      // thermocouple
public:
    bool begin() override; bool read(double& out) override;
    InstrumentDesc describe(const String& id) const override;
    const char* driver() const override { return "MAX31856"; }
};
#endif

#ifdef HEARTH_TILE_SHT45
class TileSHT45 : public SensorTile {         // temp + RH (publishes temp; see note)
public:
    bool begin() override; bool read(double& out) override;
    InstrumentDesc describe(const String& id) const override;
    const char* driver() const override { return "SHT45"; }
};
#endif

#ifdef HEARTH_TILE_SCD41
class TileSCD41 : public SensorTile {         // CO2
public:
    bool begin() override; bool read(double& out) override;
    InstrumentDesc describe(const String& id) const override;
    const char* driver() const override { return "SCD41"; }
};
#endif

#ifdef HEARTH_TILE_BMP390
class TileBMP390 : public SensorTile {        // pressure
public:
    bool begin() override; bool read(double& out) override;
    InstrumentDesc describe(const String& id) const override;
    const char* driver() const override { return "BMP390"; }
};
#endif

#ifdef HEARTH_TILE_INA228
class TileINA228 : public SensorTile {        // voltage (bus)
public:
    bool begin() override; bool read(double& out) override;
    InstrumentDesc describe(const String& id) const override;
    const char* driver() const override { return "INA228"; }
};
#endif

#ifdef HEARTH_TILE_AS7341
class TileAS7341 : public SensorTile {        // spectral (clear channel, counts)
public:
    bool begin() override; bool read(double& out) override;
    InstrumentDesc describe(const String& id) const override;
    const char* driver() const override { return "AS7341"; }
};
#endif

#ifdef HEARTH_TILE_EZO_PH
class TileEzoPh : public SensorTile {         // Atlas Scientific EZO-pH (I2C)
public:
    bool begin() override; bool read(double& out) override;
    InstrumentDesc describe(const String& id) const override;
    const char* driver() const override { return "EZO-pH"; }
private: static constexpr uint8_t kAddr = 0x63;   // default EZO-pH I2C address
};
#endif

} // namespace hearth
