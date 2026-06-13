#pragma once
// hearth_httpd — the device HTTP API (FABRIC.md §6), port 80:
//   GET  /            status page (renders pairing QR payload as text + link)
//   GET  /status      JSON status
//   GET  /snapshot    single JPEG          (camera hook)
//   GET  /stream      MJPEG                 (camera hook)
//   GET  /clips       JSON list             (SdClip)
//   GET  /clips/<f>   clip bytes            (SdClip)
//   POST /provision   {ssid,pass} -> persist + reboot
//   POST /pair        exchange QR key -> device token (HMAC self-issued)
//   GET  /config      current edge config JSON
//   POST /config      update edge config {person_threshold,retention_days,face}
//
// Auth (§6): /snapshot,/stream,/clips,/config(POST) require a valid HMAC bearer
// (hearth_auth). /provision,/pair,/ ,/status are open (pairing bootstrap). The
// host wires camera + config behaviour via the Hooks struct.

#include <Arduino.h>
#include <WebServer.h>
#include <functional>
#include "hearth_auth.h"
#include "hearth_sdclip.h"

namespace hearth {

struct EdgeConfig {
    float person_threshold = 0.60f;   // §7 config
    int   retention_days   = 14;
    bool  face             = false;
};

class Httpd {
public:
    struct Hooks {
        // Write a single JPEG to the response (camera tiers). Return false if none.
        std::function<bool(WebServer&)> snapshot;
        // Run an MJPEG loop until the client disconnects (camera tiers).
        std::function<void(WebServer&)> stream;
        // Called when /config POST changes config; host persists + applies.
        std::function<void(const EdgeConfig&)> onConfig;
        // Status page extras (capabilities JSON injected into /status).
        std::function<String()> statusExtra;
    };

    // deviceId/kind/name/fw label the status payload; lanHost/softap feed the QR.
    void begin(const String& deviceId, const char* kind, const String& name,
               const String& fw, Auth& auth, SdClip* clips,
               const String& lanHost, const String& softap,
               EdgeConfig* cfg, Hooks hooks, long* nowUnix);

    void loop();   // pump the server (call from loop())
    String httpBase() const;   // "http://<ip>"

private:
    WebServer  srv_{80};
    Auth*      auth_  = nullptr;
    SdClip*    clips_ = nullptr;
    EdgeConfig* cfg_  = nullptr;
    long*      now_   = nullptr;
    Hooks      hooks_;
    String     deviceId_, name_, fw_, lanHost_, softap_;
    const char* kind_ = "camera";

    bool authorized(const char* path);   // checks Authorization + X-Hearth-Ts
    void routeRoot();
    void routeStatus();
    void routeClipsList();
    void routeClipFile();
    void routeProvision();
    void routePair();
    void routeConfigGet();
    void routeConfigPost();
};

} // namespace hearth
