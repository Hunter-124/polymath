#pragma once
// Httpd — the device HTTP API (FABRIC.md §6) as a small blocking socket server
// (one thread per connection). Endpoints: / /status /snapshot /clips /clips/<f>
// /config (GET/POST) /pair, plus /edge/detect when --edge-hub is on. Media + clips
// + config(POST) require the HMAC bearer.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include "hearth_pi/auth.h"

namespace hearth {

struct HttpRequest {
    std::string method, path, body;
    std::map<std::string, std::string> headers;   // lower-cased keys
};
struct HttpResponse {
    int code = 200;
    std::string contentType = "application/json";
    std::string body;
};

class Httpd {
public:
    struct Hooks {
        std::function<std::string()> snapshotJpeg;   // returns JPEG bytes or ""
        std::function<std::string()> clipsListJson;  // GET /clips body
        std::function<std::string(const std::string&)> clipFile; // file -> bytes/""
        std::function<std::string()> statusExtra;    // extra status fields
        std::function<void(const std::string&)> onConfig;  // POST /config body
        std::function<std::string()> configJson;     // GET /config body
        std::function<std::string(const std::string&)> edgeDetect; // /edge/detect
    };

    Httpd(uint16_t port, std::string deviceId, std::string kind, std::string name,
          std::string fw, Auth& auth, std::string lanHost, std::string softap,
          std::function<long()> nowFn, Hooks hooks);

    void run();   // blocks; spawn in a thread

private:
    uint16_t port_;
    std::string deviceId_, kind_, name_, fw_, lanHost_, softap_;
    Auth& auth_;
    std::function<long()> now_;
    Hooks hooks_;

    HttpResponse route(const HttpRequest& r);
    bool authorized(const HttpRequest& r, const std::string& path);
};

} // namespace hearth
