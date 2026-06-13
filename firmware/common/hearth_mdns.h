#pragma once
// hearth_mdns — advertise "_hearth._tcp" on :80 and "<device_id>.local"
// (FABRIC.md §3, §6 lan_host). Hostname uses the device_id so the QR's
// "lan_host":"hearth-cam-a1b2c3.local" resolves after STA join.

#include <Arduino.h>

namespace hearth {

class Mdns {
public:
    // deviceId becomes the .local hostname; kind/name are advertised as TXT.
    void begin(const String& deviceId, const char* kind, const String& name,
               uint16_t httpPort = 80);
    String lanHost() const { return host_; }   // "<device_id>.local"
private:
    String host_;
};

} // namespace hearth
