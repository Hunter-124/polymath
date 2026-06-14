#pragma once
// hearth_ota — handle cmd/ota {"url","sha256"} (FABRIC.md §7): HTTPS fetch the
// firmware image, verify sha256 over the downloaded bytes, flash via Update, then
// reboot. On any failure it rolls back (Update aborts; no partition switch).

#include <Arduino.h>

namespace hearth {

class Ota {
public:
    // Call from the MQTT command handler for name=="ota". `payload` is the raw
    // JSON {"url","sha256"}. Returns true if flashing succeeded (device reboots).
    static bool handle(const String& payload);

    // Lower-level: fetch `url`, verify against `sha256Hex`, flash. Public for tests.
    static bool fetchVerifyFlash(const String& url, const String& sha256Hex);
};

} // namespace hearth
