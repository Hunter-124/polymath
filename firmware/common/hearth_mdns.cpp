#include "hearth_mdns.h"
#include <ESPmDNS.h>

namespace hearth {

void Mdns::begin(const String& deviceId, const char* kind, const String& name,
                 uint16_t httpPort) {
    host_ = deviceId + ".local";
    if (!MDNS.begin(deviceId.c_str())) {
        Serial.println("[mdns] begin failed");
        return;
    }
    MDNS.addService("hearth", "tcp", httpPort);
    MDNS.addServiceTxt("hearth", "tcp", "device_id", deviceId.c_str());
    MDNS.addServiceTxt("hearth", "tcp", "kind", kind);
    MDNS.addServiceTxt("hearth", "tcp", "name", name.c_str());
    // Also advertise plain http so generic browsers find the status page.
    MDNS.addService("http", "tcp", httpPort);
    Serial.printf("[mdns] %s  (_hearth._tcp :%u)\n", host_.c_str(), httpPort);
}

} // namespace hearth
