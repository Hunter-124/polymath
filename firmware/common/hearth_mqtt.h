#pragma once
// hearth_mqtt — thin PubSubClient wrapper that speaks FABRIC.md topics/payloads.
//
//   status                 retained, LWT={"online":false}              (§2)
//   announce               on connect                                  (§3)
//   event                  CameraEvent                                 (§4)
//   reading/<inst>         retained Reading                            (§5)
//   wake                   {"phrase","ts"}                             (§8)
//   cmd/<name>             subscribed; dispatched to a user callback   (§7)
//
// JSON is hand-built to keep field names byte-for-byte per the contract and to
// avoid pulling ArduinoJson into every project (callers may still use it).

#include <Arduino.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <functional>

namespace hearth {

class Mqtt {
public:
    // name/loc are human labels; fw is the firmware version string.
    void begin(const char* host, uint16_t port,
               const String& deviceId, const char* kind,
               const String& name, const String& fw);

    // Optional location label used in announce.
    void setLocation(const String& loc) { loc_ = loc; }

    // Command handler: (name, jsonPayload). name is the <name> in cmd/<name>.
    using CmdFn = std::function<void(const String& name, const String& payload)>;
    void onCommand(CmdFn fn) { cmd_ = fn; }

    // Pump connection + dispatch. Call from loop(). Reconnects with backoff and
    // (re)publishes birth + announce on every (re)connect.
    void loop();
    bool connected();

    // --- publish helpers (FABRIC.md payloads) -------------------------------

    // §3 announce. `capsJson` is the inner object for "capabilities" (may be "{}").
    // `instrumentsJson` is the array body for "instruments" (pass "" to omit).
    void publishAnnounce(const String& endpoint, const char* transport,
                         const String& capsJson, const String& instrumentsJson = "");

    // §4 CameraEvent. kind ∈ motion|person|face.
    void publishEvent(const char* evKind, float confidence,
                      const String& thumbB64, const String& clipUrl, long ts);

    // §5 Reading (retained).
    void publishReading(const String& instrumentId, double value,
                        const char* unit, const char* deviceClass, long ts);

    // §8 wake.
    void publishWake(const String& phrase, long ts);

    // Raw publish on hearth/<id>/<suffix> (suffix may contain '/').
    bool publish(const char* suffix, const String& payload, bool retained = false);

    String topic(const char* suffix) const { return base_ + suffix; }

private:
    WiFiClient   net_;
    PubSubClient mqtt_{net_};

    String   host_;
    uint16_t port_ = 1883;
    String   deviceId_, base_, name_, loc_, fw_;
    const char* kind_ = "camera";
    CmdFn    cmd_;
    uint32_t lastTry_ = 0;

    // Cached announce inputs so reconnect can re-announce automatically.
    bool   haveAnnounce_ = false;
    String anEndpoint_, anCaps_, anInstr_;
    const char* anTransport_ = "mqtt";

    bool reconnect();
    void publishBirth();
    static void trampoline(char* topic, uint8_t* payload, unsigned len);
};

} // namespace hearth
