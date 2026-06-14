#include "hearth_mqtt.h"

namespace hearth {

// PubSubClient's callback is a C function pointer, so we route through a single
// instance. Only one Mqtt per firmware in practice (one hub link per device).
static Mqtt* s_self = nullptr;

void Mqtt::begin(const char* host, uint16_t port,
                 const String& deviceId, const char* kind,
                 const String& name, const String& fw) {
    s_self    = this;
    host_     = host;
    port_     = port;
    deviceId_ = deviceId;
    kind_     = kind;
    name_     = name;
    fw_       = fw;
    base_     = String("hearth/") + deviceId + "/";

    mqtt_.setServer(host_.c_str(), port_);
    mqtt_.setBufferSize(2048);            // events carry small base64 thumbnails
    mqtt_.setKeepAlive(20);
    mqtt_.setCallback(&Mqtt::trampoline);
}

void Mqtt::trampoline(char* topic, uint8_t* payload, unsigned len) {
    if (!s_self || !s_self->cmd_) return;
    String t(topic);
    String body; body.reserve(len);
    for (unsigned i = 0; i < len; ++i) body += (char)payload[i];
    // topic = hearth/<id>/cmd/<name>  -> extract <name>
    int cmdPos = t.indexOf("/cmd/");
    if (cmdPos < 0) return;
    s_self->cmd_(t.substring(cmdPos + 5), body);
}

bool Mqtt::connected() { return mqtt_.connected(); }

void Mqtt::publishBirth() {
    // §2 retained presence.
    String j = "{";
    j += "\"device_id\":\"" + deviceId_ + "\",";
    j += "\"kind\":\""      + String(kind_) + "\",";
    j += "\"name\":\""      + name_ + "\",";
    j += "\"online\":true,";
    j += "\"fw\":\""        + fw_ + "\",";
    j += "\"ts\":0}";
    publish("status", j, /*retained=*/true);
}

bool Mqtt::reconnect() {
    // LWT: same topic, online=false, retained — ungraceful drop marks offline.
    String willTopic = base_ + "status";
    String willMsg   = String("{\"device_id\":\"") + deviceId_ +
                       "\",\"kind\":\"" + kind_ + "\",\"online\":false,\"ts\":0}";
    bool ok = mqtt_.connect(deviceId_.c_str(), nullptr, nullptr,
                            willTopic.c_str(), 0, /*retained=*/true,
                            willMsg.c_str(), /*cleanSession=*/true);
    if (!ok) return false;

    publishBirth();
    if (haveAnnounce_)
        publishAnnounce(anEndpoint_, anTransport_, anCaps_, anInstr_);

    // Subscribe to all hub->device commands (§7).
    String sub = base_ + "cmd/#";
    mqtt_.subscribe(sub.c_str());
    Serial.printf("[mqtt] connected %s\n", deviceId_.c_str());
    return true;
}

void Mqtt::loop() {
    if (!mqtt_.connected()) {
        uint32_t now = millis();
        if (now - lastTry_ < 3000) return;   // backoff
        lastTry_ = now;
        reconnect();
        return;
    }
    mqtt_.loop();
}

bool Mqtt::publish(const char* suffix, const String& payload, bool retained) {
    if (!mqtt_.connected()) return false;
    String t = base_ + suffix;
    return mqtt_.publish(t.c_str(), (const uint8_t*)payload.c_str(),
                         payload.length(), retained);
}

void Mqtt::publishAnnounce(const String& endpoint, const char* transport,
                           const String& capsJson, const String& instrumentsJson) {
    // Cache for auto re-announce on reconnect.
    haveAnnounce_ = true;
    anEndpoint_   = endpoint;
    anTransport_  = transport;
    anCaps_       = capsJson;
    anInstr_      = instrumentsJson;

    String j = "{";
    j += "\"device_id\":\"" + deviceId_ + "\",";
    j += "\"kind\":\""      + String(kind_) + "\",";
    j += "\"name\":\""      + name_ + "\",";
    j += "\"location\":\""  + loc_ + "\",";
    j += "\"fw\":\""        + fw_ + "\",";
    j += "\"endpoint\":\""  + endpoint + "\",";
    j += "\"transport\":\"" + String(transport) + "\",";
    j += "\"capabilities\":" + capsJson;
    if (instrumentsJson.length())
        j += ",\"instruments\":" + instrumentsJson;
    j += "}";
    publish("announce", j, /*retained=*/false);
}

void Mqtt::publishEvent(const char* evKind, float confidence,
                        const String& thumbB64, const String& clipUrl, long ts) {
    String j = "{";
    j += "\"device_id\":\"" + deviceId_ + "\",";
    j += "\"kind\":\""      + String(evKind) + "\",";
    j += "\"confidence\":"  + String(confidence, 2) + ",";
    j += "\"thumb_b64\":\"" + thumbB64 + "\",";
    j += "\"clip_url\":\""  + clipUrl + "\",";
    j += "\"ts\":"          + String(ts) + "}";
    publish("event", j, /*retained=*/false);
}

void Mqtt::publishReading(const String& instrumentId, double value,
                          const char* unit, const char* deviceClass, long ts) {
    String j = "{";
    j += "\"instrument_id\":\"" + instrumentId + "\",";
    j += "\"device_id\":\""     + deviceId_ + "\",";
    j += "\"value\":"           + String(value, 4) + ",";
    j += "\"unit\":\""          + String(unit) + "\",";
    j += "\"device_class\":\""  + String(deviceClass) + "\",";
    j += "\"ts\":"              + String(ts) + "}";
    String suffix = String("reading/") + instrumentId;
    publish(suffix.c_str(), j, /*retained=*/true);
}

void Mqtt::publishWake(const String& phrase, long ts) {
    String j = String("{\"phrase\":\"") + phrase + "\",\"ts\":" + String(ts) + "}";
    publish("wake", j, /*retained=*/false);
}

} // namespace hearth
