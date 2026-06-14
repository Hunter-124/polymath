#pragma once
// Sensor tile interface — each tile drives one I2C/SPI instrument and maps to a
// single Hearth instrument `id`. Tiles publish Reading (§5) and self-describe for
// announce (§3). Concrete tiles live in tiles/. Enable them in config.h.
//
// The instrument_id convention (FABRIC.md §3 example) is
//   "<device_id>_<class>_<unit>", e.g. "hearth-hmm-44ccbb_mass_g".

#include <Arduino.h>

namespace hearth {

// Self-description used to build the announce "instruments" array entry (§3).
struct InstrumentDesc {
    String id;            // full instrument_id
    String name;          // human label
    int    channel;       // 0-based index on this module
    String unit;          // "g","C","%","ppm","Pa","V","A","","pH",...
    String deviceClass;   // "mass","temperature","humidity","co2",...
    double expectedMin;
    double expectedMax;
};

class SensorTile {
public:
    virtual ~SensorTile() = default;

    // Bring up the sensor over the already-begun Wire bus. Return false if absent.
    virtual bool begin() = 0;

    // Read the current value into `out` (in `unit`). Return false on a read error
    // (caller skips publishing that cycle). Implementations may block briefly.
    virtual bool read(double& out) = 0;

    // Describe this tile for announce. `deviceId` is the module's device_id.
    virtual InstrumentDesc describe(const String& deviceId) const = 0;

    // Short driver name for logs.
    virtual const char* driver() const = 0;
};

} // namespace hearth
