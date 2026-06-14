#pragma once
// Mqtt — FABRIC.md MQTT client. Backed by libmosquitto when found at build time
// (HEARTH_HAVE_MOSQUITTO); otherwise a no-op stub so the binary still builds and
// the HTTP/NVR path works (events then only flow via HTTP push, FABRIC.md §4).

#include <cstdint>
#include <functional>
#include <string>

namespace hearth {

class Mqtt {
public:
    Mqtt(std::string host, uint16_t port, std::string deviceId,
         std::string kind, std::string name, std::string fw);
    ~Mqtt();

    void setLocation(const std::string& l) { location_ = l; }
    void onCommand(std::function<void(const std::string&, const std::string&)> fn) { cmd_ = std::move(fn); }

    bool connect();          // birth + LWT + subscribe cmd/#
    void loop();             // pump (call periodically)

    void publishAnnounce(const std::string& endpoint, const std::string& capsJson,
                         const std::string& transport = "mqtt");
    void publishEvent(const std::string& evKind, float conf,
                      const std::string& thumbB64, const std::string& clipUrl, long ts);
    bool publish(const std::string& suffix, const std::string& payload, bool retained = false);
    std::string topic(const std::string& suffix) const { return base_ + suffix; }
    bool available() const;  // true if a real broker backend is compiled in

private:
    struct Impl; Impl* impl_ = nullptr;
    std::string host_, deviceId_, base_, kind_, name_, location_, fw_;
    uint16_t port_;
    std::function<void(const std::string&, const std::string&)> cmd_;
};

} // namespace hearth
