#include "hearth_pi/mqtt.h"
#include <cstdio>

#ifdef HEARTH_HAVE_MOSQUITTO
#include <mosquitto.h>
#endif

namespace hearth {

struct Mqtt::Impl {
#ifdef HEARTH_HAVE_MOSQUITTO
    struct mosquitto* m = nullptr;
#endif
    bool connected = false;
};

static std::string birthJson(const std::string& id, const std::string& kind,
                             const std::string& name, const std::string& fw, bool online) {
    std::string j = "{\"device_id\":\"" + id + "\",\"kind\":\"" + kind + "\"";
    if (online) j += ",\"name\":\"" + name + "\",\"online\":true,\"fw\":\"" + fw + "\"";
    else        j += ",\"online\":false";
    j += ",\"ts\":0}";
    return j;
}

Mqtt::Mqtt(std::string host, uint16_t port, std::string deviceId,
           std::string kind, std::string name, std::string fw)
    : host_(std::move(host)), deviceId_(std::move(deviceId)), kind_(std::move(kind)),
      name_(std::move(name)), fw_(std::move(fw)), port_(port) {
    base_ = "hearth/" + deviceId_ + "/";
    impl_ = new Impl();
}

Mqtt::~Mqtt() {
#ifdef HEARTH_HAVE_MOSQUITTO
    if (impl_->m) { mosquitto_destroy(impl_->m); mosquitto_lib_cleanup(); }
#endif
    delete impl_;
}

bool Mqtt::available() const {
#ifdef HEARTH_HAVE_MOSQUITTO
    return true;
#else
    return false;
#endif
}

bool Mqtt::connect() {
#ifdef HEARTH_HAVE_MOSQUITTO
    mosquitto_lib_init();
    impl_->m = mosquitto_new(deviceId_.c_str(), true, this);
    if (!impl_->m) return false;
    // LWT
    std::string willTopic = base_ + "status";
    std::string will = birthJson(deviceId_, kind_, name_, fw_, false);
    mosquitto_will_set(impl_->m, willTopic.c_str(), will.size(), will.data(), 0, true);
    mosquitto_message_callback_set(impl_->m, [](struct mosquitto*, void* ud,
                                                const struct mosquitto_message* msg) {
        auto* self = static_cast<Mqtt*>(ud);
        std::string t(msg->topic ? msg->topic : "");
        auto pos = t.find("/cmd/");
        if (pos == std::string::npos || !self) return;
        std::string name = t.substr(pos + 5);
        std::string payload((const char*)msg->payload, msg->payloadlen);
        if (self->cmd_) self->cmd_(name, payload);
    });
    if (mosquitto_connect(impl_->m, host_.c_str(), port_, 20) != MOSQ_OK) return false;
    // birth + subscribe
    std::string birth = birthJson(deviceId_, kind_, name_, fw_, true);
    mosquitto_publish(impl_->m, nullptr, (base_ + "status").c_str(), birth.size(), birth.data(), 0, true);
    mosquitto_subscribe(impl_->m, nullptr, (base_ + "cmd/#").c_str(), 0);
    impl_->connected = true;
    mosquitto_loop_start(impl_->m);
    return true;
#else
    fprintf(stderr, "[mqtt] libmosquitto not compiled in; events flow via HTTP push only\n");
    return false;
#endif
}

void Mqtt::loop() {
#ifndef HEARTH_HAVE_MOSQUITTO
    // no-op; loop_start() handles the network thread when mosquitto is present
#endif
}

bool Mqtt::publish(const std::string& suffix, const std::string& payload, bool retained) {
#ifdef HEARTH_HAVE_MOSQUITTO
    if (!impl_->connected) return false;
    std::string t = base_ + suffix;
    return mosquitto_publish(impl_->m, nullptr, t.c_str(), payload.size(),
                             payload.data(), 0, retained) == MOSQ_OK;
#else
    (void)suffix; (void)payload; (void)retained; return false;
#endif
}

void Mqtt::publishAnnounce(const std::string& endpoint, const std::string& capsJson,
                           const std::string& transport) {
    std::string j = "{\"device_id\":\"" + deviceId_ + "\",\"kind\":\"" + kind_ +
                    "\",\"name\":\"" + name_ + "\",\"location\":\"" + location_ +
                    "\",\"fw\":\"" + fw_ + "\",\"endpoint\":\"" + endpoint +
                    "\",\"transport\":\"" + transport + "\",\"capabilities\":" + capsJson + "}";
    publish("announce", j, false);
}

void Mqtt::publishEvent(const std::string& evKind, float conf,
                        const std::string& thumbB64, const std::string& clipUrl, long ts) {
    char cbuf[16]; snprintf(cbuf, sizeof(cbuf), "%.2f", conf);
    std::string j = "{\"device_id\":\"" + deviceId_ + "\",\"kind\":\"" + evKind +
                    "\",\"confidence\":" + cbuf + ",\"thumb_b64\":\"" + thumbB64 +
                    "\",\"clip_url\":\"" + clipUrl + "\",\"ts\":" + std::to_string(ts) + "}";
    publish("event", j, false);
}

} // namespace hearth
