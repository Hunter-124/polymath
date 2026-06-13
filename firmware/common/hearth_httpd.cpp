#include "hearth_httpd.h"
#include <WiFi.h>
#include <Preferences.h>
#include <FS.h>

namespace hearth {

String Httpd::httpBase() const {
    return String("http://") + WiFi.localIP().toString();
}

bool Httpd::authorized(const char* path) {
    String hdr = srv_.header("Authorization");
    long ts = srv_.header("X-Hearth-Ts").toInt();
    long now = now_ ? *now_ : 0;
    return auth_ && auth_->verify(hdr, String(path), ts, now);
}

void Httpd::begin(const String& deviceId, const char* kind, const String& name,
                  const String& fw, Auth& auth, SdClip* clips,
                  const String& lanHost, const String& softap,
                  EdgeConfig* cfg, Hooks hooks, long* nowUnix) {
    deviceId_ = deviceId; kind_ = kind; name_ = name; fw_ = fw;
    auth_ = &auth; clips_ = clips; cfg_ = cfg; hooks_ = hooks;
    lanHost_ = lanHost; softap_ = softap; now_ = nowUnix;

    // We read these request headers in handlers.
    const char* wanted[] = {"Authorization", "X-Hearth-Ts"};
    srv_.collectHeaders(wanted, 2);

    srv_.on("/",          HTTP_GET,  [this]{ routeRoot(); });
    srv_.on("/status",    HTTP_GET,  [this]{ routeStatus(); });
    srv_.on("/clips",     HTTP_GET,  [this]{ routeClipsList(); });
    srv_.on("/provision", HTTP_POST, [this]{ routeProvision(); });
    srv_.on("/pair",      HTTP_POST, [this]{ routePair(); });
    srv_.on("/config",    HTTP_GET,  [this]{ routeConfigGet(); });
    srv_.on("/config",    HTTP_POST, [this]{ routeConfigPost(); });

    if (hooks_.snapshot)
        srv_.on("/snapshot", HTTP_GET, [this]{
            if (!authorized("/snapshot")) { srv_.send(401, "application/json", "{\"ok\":false}"); return; }
            if (!hooks_.snapshot(srv_)) srv_.send(503, "application/json", "{\"ok\":false}");
        });
    if (hooks_.stream)
        srv_.on("/stream", HTTP_GET, [this]{
            if (!authorized("/stream")) { srv_.send(401, "application/json", "{\"ok\":false}"); return; }
            hooks_.stream(srv_);
        });

    // /clips/<file> — match the prefix and authorize, then stream the file.
    srv_.onNotFound([this]{
        String uri = srv_.uri();
        if (uri.startsWith("/clips/")) { routeClipFile(); return; }
        srv_.send(404, "application/json", "{\"ok\":false}");
    });

    srv_.begin();
    Serial.printf("[httpd] device API up at %s/\n", httpBase().c_str());
}

void Httpd::loop() { srv_.handleClient(); }

void Httpd::routeRoot() {
    String qr = auth_->qrPayload(deviceId_, kind_, softap_, lanHost_);
    String html = "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>";
    html += "<h3>Hearth " + String(kind_) + " — " + name_ + "</h3>";
    html += "<p><code>" + deviceId_ + "</code> fw " + fw_ + "</p>";
    html += "<p>Pair payload (QR):</p><pre style='white-space:pre-wrap'>" + qr + "</pre>";
    html += "<p><a href=/status>/status</a>";
    if (hooks_.snapshot) html += " &middot; /snapshot";
    if (hooks_.stream)   html += " &middot; /stream";
    if (clips_)          html += " &middot; <a href=/clips>/clips</a>";
    html += "</p>";
    srv_.send(200, "text/html", html);
}

void Httpd::routeStatus() {
    String j = "{";
    j += "\"device_id\":\"" + deviceId_ + "\",";
    j += "\"kind\":\""      + String(kind_) + "\",";
    j += "\"name\":\""      + name_ + "\",";
    j += "\"fw\":\""        + fw_ + "\",";
    j += "\"ip\":\""        + WiFi.localIP().toString() + "\",";
    j += "\"rssi\":"        + String((long)WiFi.RSSI()) + ",";
    j += "\"lan_host\":\""  + lanHost_ + "\"";
    if (hooks_.statusExtra) { String x = hooks_.statusExtra(); if (x.length()) j += "," + x; }
    j += "}";
    srv_.send(200, "application/json", j);
}

void Httpd::routeClipsList() {
    if (!authorized("/clips")) { srv_.send(401, "application/json", "{\"ok\":false}"); return; }
    if (!clips_) { srv_.send(200, "application/json", "[]"); return; }
    srv_.send(200, "application/json", clips_->listJson());
}

void Httpd::routeClipFile() {
    if (!authorized(srv_.uri().c_str())) { srv_.send(401, "application/json", "{\"ok\":false}"); return; }
    if (!clips_ || !clips_->fs()) { srv_.send(404, "application/json", "{\"ok\":false}"); return; }
    String file = srv_.uri().substring(strlen("/clips/"));
    if (file.indexOf("..") >= 0) { srv_.send(400, "application/json", "{\"ok\":false}"); return; }
    String path = String(clips_->dir()) + "/" + file;
    File f = clips_->fs()->open(path, FILE_READ);
    if (!f) { srv_.send(404, "application/json", "{\"ok\":false}"); return; }
    srv_.streamFile(f, "video/x-motion-jpeg");
    f.close();
}

void Httpd::routeProvision() {
    String ssid = srv_.arg("ssid"), pass = srv_.arg("pass");
    if (!ssid.length()) { srv_.send(400, "application/json", "{\"ok\":false}"); return; }
    srv_.send(200, "application/json", "{\"ok\":true}");
    // Persist via the same NVS namespace/keys hearth_wifi uses, then reboot to
    // re-run STA join with the new creds.
    Preferences p; p.begin("hearth", false);
    p.putString("wifi_ssid", ssid); p.putString("wifi_pass", pass); p.end();
    delay(200); ESP.restart();
}

void Httpd::routePair() {
    // §6: phone POSTs the QR `key` to prove possession; we return a device token.
    // The token here is HMAC(key, "/pair"+ts) — the phone caches {device_id,key}
    // and self-derives per-request bearers, so /pair just confirms the key works.
    long ts = srv_.header("X-Hearth-Ts").toInt();
    if (!authorized("/pair")) { srv_.send(401, "application/json", "{\"ok\":false}"); return; }
    String tok = auth_->sign("/pair", ts);
    String j = "{\"ok\":true,\"device_id\":\"" + deviceId_ + "\",\"token\":\"" + tok + "\"}";
    srv_.send(200, "application/json", j);
}

void Httpd::routeConfigGet() {
    if (!cfg_) { srv_.send(200, "application/json", "{}"); return; }
    String j = "{";
    j += "\"person_threshold\":" + String(cfg_->person_threshold, 2) + ",";
    j += "\"retention_days\":"   + String(cfg_->retention_days) + ",";
    j += "\"face\":" + String(cfg_->face ? "true" : "false") + "}";
    srv_.send(200, "application/json", j);
}

void Httpd::routeConfigPost() {
    if (!authorized("/config")) { srv_.send(401, "application/json", "{\"ok\":false}"); return; }
    if (!cfg_) { srv_.send(200, "application/json", "{\"ok\":true}"); return; }
    if (srv_.hasArg("person_threshold")) cfg_->person_threshold = srv_.arg("person_threshold").toFloat();
    if (srv_.hasArg("retention_days"))   cfg_->retention_days   = srv_.arg("retention_days").toInt();
    if (srv_.hasArg("face"))             cfg_->face = (srv_.arg("face") == "true" || srv_.arg("face") == "1");
    if (hooks_.onConfig) hooks_.onConfig(*cfg_);
    srv_.send(200, "application/json", "{\"ok\":true}");
}

} // namespace hearth
