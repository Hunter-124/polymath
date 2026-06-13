#pragma once
// hearth_wifi — STA connect with SoftAP provisioning fallback (FABRIC.md §6).
//
// On boot: load creds from NVS, try STA. If none/failed, start SoftAP
//   "Hearth-Setup-<hex6>" + a captive server accepting POST /provision {ssid,pass}.
// On provision: persist creds to NVS, reboot into STA.
//
// The captive provisioning server is intentionally tiny and self-contained so it
// works before the full hearth_httpd comes up. After STA join, hearth_httpd owns
// /provision on port 80 (and re-persists), so creds can be changed later too.

#include <Arduino.h>
#include "hearth_id.h"

namespace hearth {

class Wifi {
public:
    // kind feeds the SoftAP SSID suffix + nothing else. autoConnectMs is the STA
    // join timeout before falling back to SoftAP provisioning.
    void begin(Kind kind, uint32_t autoConnectMs = 20000);

    bool   isStation() const { return mode_ == Mode::Station; }
    bool   isProvisioning() const { return mode_ == Mode::SoftAP; }
    String ssid() const { return ssid_; }
    String softApSsid() const { return apSsid_; }

    // Persist creds (called by hearth_httpd /provision too) and reboot to apply.
    static void saveCredsAndReboot(const String& ssid, const String& pass);

    // Pump the captive provisioning server when in SoftAP mode (call from loop()).
    void loop();

private:
    enum class Mode { Station, SoftAP };
    Mode   mode_ = Mode::SoftAP;
    String ssid_;
    String apSsid_;

    bool loadCreds(String& ssid, String& pass);
    bool tryStation(const String& ssid, const String& pass, uint32_t timeoutMs);
    void startSoftAp(Kind kind);
};

} // namespace hearth
