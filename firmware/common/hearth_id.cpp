#include "hearth_id.h"
#include <WiFi.h>

namespace hearth {

const char* kindTag(Kind k) {
    switch (k) {
        case Kind::Camera:     return "cam";
        case Kind::VoiceSat:   return "mic";
        case Kind::Instrument: return "hmm";
        case Kind::Panel:      return "pnl";
    }
    return "dev";
}

const char* kindString(Kind k) {
    switch (k) {
        case Kind::Camera:     return "camera";
        case Kind::VoiceSat:   return "voice_sat";
        case Kind::Instrument: return "instrument";
        case Kind::Panel:      return "panel";
    }
    return "device";
}

String macHex6() {
    // WiFi.macAddress() needs the driver up; readMacAddress works pre-connect.
    uint8_t mac[6] = {0};
    WiFi.macAddress(mac);                  // STA MAC; stable per chip
    char buf[7];
    snprintf(buf, sizeof(buf), "%02x%02x%02x", mac[3], mac[4], mac[5]);
    return String(buf);
}

String deviceId(Kind k) {
    static String cached;
    if (cached.length()) return cached;
    cached = String("hearth-") + kindTag(k) + "-" + macHex6();
    return cached;
}

} // namespace hearth
