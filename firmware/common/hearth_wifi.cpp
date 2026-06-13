#include "hearth_wifi.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

namespace hearth {

static const char* kNs   = "hearth";
static const char* kSsid = "wifi_ssid";
static const char* kPass = "wifi_pass";

// Captive-portal singletons, only alive while provisioning.
static WebServer*  s_ap   = nullptr;
static DNSServer*  s_dns  = nullptr;

bool Wifi::loadCreds(String& ssid, String& pass) {
    Preferences p;
    p.begin(kNs, /*readOnly=*/true);
    ssid = p.getString(kSsid, "");
    pass = p.getString(kPass, "");
    p.end();
    return ssid.length() > 0;
}

void Wifi::saveCredsAndReboot(const String& ssid, const String& pass) {
    Preferences p;
    p.begin(kNs, false);
    p.putString(kSsid, ssid);
    p.putString(kPass, pass);
    p.end();
    delay(200);
    ESP.restart();
}

bool Wifi::tryStation(const String& ssid, const String& pass, uint32_t timeoutMs) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(250);
    return WiFi.status() == WL_CONNECTED;
}

static const char* kPortalHtml =
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<h3>Hearth Setup</h3><form method=POST action=/provision>"
    "SSID<br><input name=ssid><br>Password<br><input name=pass type=password><br><br>"
    "<button>Join Wi-Fi</button></form>";

void Wifi::startSoftAp(Kind kind) {
    mode_   = Mode::SoftAP;
    apSsid_ = String("Hearth-Setup-") + macHex6();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSsid_.c_str());                 // open AP; provisioning only
    IPAddress ip = WiFi.softAPIP();

    s_dns = new DNSServer();
    s_dns->start(53, "*", ip);                    // captive: resolve all to us

    s_ap = new WebServer(80);
    s_ap->on("/", HTTP_GET, []() { s_ap->send(200, "text/html", kPortalHtml); });
    s_ap->on("/provision", HTTP_POST, []() {
        String ssid = s_ap->arg("ssid");
        String pass = s_ap->arg("pass");
        if (!ssid.length()) { s_ap->send(400, "application/json", "{\"ok\":false}"); return; }
        s_ap->send(200, "application/json", "{\"ok\":true}");
        Wifi::saveCredsAndReboot(ssid, pass);     // persists + reboots into STA
    });
    // Captive-portal probe endpoints -> bounce to "/".
    s_ap->onNotFound([]() { s_ap->send(200, "text/html", kPortalHtml); });
    s_ap->begin();
    Serial.printf("[wifi] provisioning AP up: %s  http://%s/\n",
                  apSsid_.c_str(), ip.toString().c_str());
}

void Wifi::begin(Kind kind, uint32_t autoConnectMs) {
    String ssid, pass;
    if (loadCreds(ssid, pass) && tryStation(ssid, pass, autoConnectMs)) {
        mode_ = Mode::Station;
        ssid_ = ssid;
        Serial.printf("[wifi] STA up: %s  ip=%s\n",
                      ssid.c_str(), WiFi.localIP().toString().c_str());
        return;
    }
    startSoftAp(kind);
}

void Wifi::loop() {
    if (mode_ != Mode::SoftAP) return;
    if (s_dns) s_dns->processNextRequest();
    if (s_ap)  s_ap->handleClient();
}

} // namespace hearth
