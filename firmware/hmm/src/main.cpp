// hmm — Hearth Measurement Module (ESP32-S3 + Qwiic/STEMMA-QT I2C sensors).
// ----------------------------------------------------------------------------
// A bag of SensorTile drivers; each enabled tile maps to one instrument id,
// announces itself (FABRIC.md §3) and publishes a retained Reading (§5) on a
// timer. Reuses firmware/common for wifi/provisioning/mqtt/id. Enable tiles in
// config.h; add their driver libs in platformio.ini.

#include <Arduino.h>
#include <Wire.h>
#include <vector>
#include <time.h>

#include "config.h"
#include "sensor_tile.h"
#include "tiles.h"

#include "hearth_id.h"
#include "hearth_wifi.h"
#include "hearth_mdns.h"
#include "hearth_mqtt.h"
#include "hearth_auth.h"
#include "hearth_ota.h"

static const char* FW = "0.2.0";
using namespace hearth;

static Wifi  wifi;
static Auth  auth;
static Mdns  mdns;
static Mqtt  mqtt;
static long  nowUnix = 0;
static String deviceId;

static std::vector<SensorTile*> tiles;   // active tiles (begin() succeeded)

static void buildTiles() {
    // Instantiate every enabled tile; keep only those whose sensor is present.
    std::vector<SensorTile*> all;
#ifdef HEARTH_TILE_NAU7802
    all.push_back(new TileNAU7802());
#endif
#ifdef HEARTH_TILE_MAX31865
    all.push_back(new TileMAX31865());
#endif
#ifdef HEARTH_TILE_MAX31856
    all.push_back(new TileMAX31856());
#endif
#ifdef HEARTH_TILE_SHT45
    all.push_back(new TileSHT45());
#endif
#ifdef HEARTH_TILE_SCD41
    all.push_back(new TileSCD41());
#endif
#ifdef HEARTH_TILE_BMP390
    all.push_back(new TileBMP390());
#endif
#ifdef HEARTH_TILE_INA228
    all.push_back(new TileINA228());
#endif
#ifdef HEARTH_TILE_AS7341
    all.push_back(new TileAS7341());
#endif
#ifdef HEARTH_TILE_EZO_PH
    all.push_back(new TileEzoPh());
#endif
    for (auto* t : all) {
        if (t->begin()) { tiles.push_back(t); Serial.printf("[tile] %s up\n", t->driver()); }
        else            { Serial.printf("[tile] %s absent\n", t->driver()); delete t; }
    }
}

// Build the announce "instruments":[...] array from the active tiles (§3).
static String instrumentsJson() {
    String j = "[";
    for (size_t i = 0; i < tiles.size(); ++i) {
        InstrumentDesc d = tiles[i]->describe(deviceId);
        if (i) j += ",";
        j += "{";
        j += "\"id\":\""           + d.id + "\",";
        j += "\"name\":\""         + d.name + "\",";
        j += "\"channel\":"        + String(d.channel) + ",";
        j += "\"unit\":\""         + d.unit + "\",";
        j += "\"device_class\":\"" + d.deviceClass + "\",";
        j += "\"expected_min\":"   + String(d.expectedMin) + ",";
        j += "\"expected_max\":"   + String(d.expectedMax);
        j += "}";
    }
    j += "]";
    return j;
}

static void onCommand(const String& name, const String& payload) {
    if (name == "identify") {
        pinMode(LED_BUILTIN, OUTPUT);
        for (int i=0;i<6;i++){ digitalWrite(LED_BUILTIN, i&1); delay(120);}
    } else if (name == "ota") {
        Ota::handle(payload);
    }
}

void setup() {
    Serial.begin(115200); delay(200);
    auth.begin();

    wifi.begin(Kind::Instrument);
    if (wifi.isProvisioning()) { Serial.println("[boot] provisioning"); return; }

    deviceId = hearth::deviceId(Kind::Instrument);
    configTime(0, 0, "pool.ntp.org");

    Wire.begin(HEARTH_I2C_SDA, HEARTH_I2C_SCL);
    buildTiles();

    mdns.begin(deviceId, "instrument", HEARTH_DEVICE_NAME);
    mqtt.begin(HEARTH_MQTT_HOST, HEARTH_MQTT_PORT, deviceId, "instrument",
               HEARTH_DEVICE_NAME, FW);
    mqtt.setLocation(HEARTH_LOCATION);
    mqtt.onCommand(onCommand);

    mqtt.loop();
    // Instruments announce with the "instruments" array (§3); no media caps.
    mqtt.publishAnnounce(String("http://") + WiFi.localIP().toString(), "mqtt",
                         "{}", instrumentsJson());
    Serial.printf("[boot] HMM %s with %u tile(s)\n", deviceId.c_str(), (unsigned)tiles.size());
}

static uint32_t lastRead = 0;

void loop() {
    if (wifi.isProvisioning()) { wifi.loop(); return; }
    mqtt.loop();
    time_t t = time(nullptr); if (t > 1700000000) nowUnix = (long)t;

    if (millis() - lastRead > (uint32_t)HEARTH_READ_PERIOD_S * 1000) {
        lastRead = millis();
        for (auto* tile : tiles) {
            double v;
            if (!tile->read(v)) continue;
            InstrumentDesc d = tile->describe(deviceId);
            mqtt.publishReading(d.id, v, d.unit.c_str(), d.deviceClass.c_str(), nowUnix);
            Serial.printf("[reading] %s = %.4f %s\n", d.id.c_str(), v, d.unit.c_str());
        }
    }
}
