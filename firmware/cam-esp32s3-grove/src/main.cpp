// cam-esp32s3-grove — Hearth Standard cam.
// ----------------------------------------------------------------------------
// XIAO ESP32-S3 (Wi-Fi host) + Grove Vision AI Module V2 (YOLOv8 person detect
// over I2C via SSCMA). The Grove module captures + infers; the host reads the
// top person box + the JPEG preview, records clips to SD, and publishes the
// FABRIC.md §4 CameraEvent. Higher-confidence "reliable" filter. Reuses
// firmware/common for wifi/mqtt/id/httpd/auth/sdclip/ota.

#include <Arduino.h>
#include <SD_MMC.h>
#include <time.h>

#include "config.h"
#include "grove_vision.h"

#include "hearth_id.h"
#include "hearth_wifi.h"
#include "hearth_mdns.h"
#include "hearth_mqtt.h"
#include "hearth_auth.h"
#include "hearth_httpd.h"
#include "hearth_sdclip.h"
#include "hearth_ota.h"
#include <mbedtls/base64.h>

static const char* FW = "0.2.0";
using namespace hearth;

static Wifi   wifi;
static Auth   auth;
static Mdns   mdns;
static Mqtt   mqtt;
static Httpd  httpd;
static SdClip clips;
static EdgeConfig edgeCfg;
static GroveVision grove;
static long   nowUnix = 0;
static String deviceId;

// ---- HTTP hooks: snapshot/stream from the Grove module's JPEG preview -------
static bool hookSnapshot(WebServer& s) {
    size_t len = 0; const uint8_t* jpg = grove.lastJpeg(len);
    if (!jpg || !len) return false;
    s.setContentLength(len);
    s.send(200, "image/jpeg", "");
    s.sendContent((const char*)jpg, len);
    return true;
}
static void hookStream(WebServer& s) {
    WiFiClient client = s.client();
    client.print("HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace;boundary=frame\r\n"
                 "Access-Control-Allow-Origin: *\r\n\r\n");
    while (client.connected()) {
        grove.poll();
        size_t len = 0; const uint8_t* jpg = grove.lastJpeg(len);
        if (jpg && len) {
            client.printf("\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                          (unsigned)len);
            client.write(jpg, len);
        }
        delay(60);
    }
}
static void onConfig(const EdgeConfig& c) { clips.setRetentionDays(c.retention_days); }
static String statusExtra() {
    return String("\"capabilities\":{\"stream\":true,\"snapshot\":true,\"clips\":true,"
                  "\"person_detect\":\"reliable\",\"resolution\":\"640x480\",\"sd\":") +
           (clips.ready() ? "true}" : "false}");
}
static String capsJson() {
    return String("{\"stream\":true,\"snapshot\":true,\"clips\":true,"
                  "\"person_detect\":\"reliable\",\"resolution\":\"640x480\",\"sd\":") +
           (clips.ready() ? "true}" : "false}");
}

static void onCommand(const String& name, const String& payload) {
    if (name == "config") {
        int i = payload.indexOf("\"person_threshold\"");
        if (i >= 0) { i = payload.indexOf(':', i); if (i>=0) edgeCfg.person_threshold = payload.substring(i+1).toFloat(); }
        i = payload.indexOf("\"retention_days\"");
        if (i >= 0) { i = payload.indexOf(':', i); if (i>=0) edgeCfg.retention_days = payload.substring(i+1).toInt(); }
        edgeCfg.face = payload.indexOf("\"face\":true") >= 0;
        onConfig(edgeCfg);
    } else if (name == "identify") {
        pinMode(LED_BUILTIN, OUTPUT);
        for (int i=0;i<6;i++){ digitalWrite(LED_BUILTIN, i&1); delay(120); }
    } else if (name == "ota") {
        Ota::handle(payload);
    }
}

static String b64(const uint8_t* d, size_t n) {
    if (!d || !n) return "";
    size_t cap = ((n + 2) / 3) * 4 + 4;
    if (cap > 60000) return "";
    uint8_t* tmp = (uint8_t*)malloc(cap); if (!tmp) return "";
    size_t olen = 0; String out;
    if (mbedtls_base64_encode(tmp, cap, &olen, d, n) == 0) out = String((const char*)tmp, olen);
    free(tmp); return out;
}

static void recordAndPublish(float conf) {
    long ts = (nowUnix > 0) ? nowUnix : (long)(millis() / 1000);
    String file = clips.beginClip(ts);
    String thumb;
    if (file.length()) {
        for (int i = 0; i < 24; ++i) {
            grove.poll();
            size_t len = 0; const uint8_t* jpg = grove.lastJpeg(len);
            if (jpg && len) { if (i == 0) thumb = b64(jpg, len); clips.addFrame(jpg, len); }
            delay(60);
        }
        clips.endClip();
    }
    String url = file.length() ? SdClip::clipUrl(httpd.httpBase(), file) : "";
    mqtt.publishEvent("person", conf, thumb, url, ts);
    Serial.printf("[event] person conf=%.2f clip=%s\n", conf, url.c_str());
}

void setup() {
    Serial.begin(115200);
    delay(200);
    auth.begin();

    wifi.begin(Kind::Camera);
    if (wifi.isProvisioning()) { Serial.println("[boot] provisioning"); return; }

    deviceId = hearth::deviceId(Kind::Camera);
    configTime(0, 0, "pool.ntp.org");

    grove.begin(HEARTH_GROVE_I2C_ADDR, HEARTH_I2C_SDA, HEARTH_I2C_SCL);
    if (!grove.ready())
        Serial.println("[grove] module not detected — SSCMA stub (person detect disabled)");

    if (SD_MMC.begin("/sdcard", true)) clips.begin(SD_MMC);
    else Serial.println("[sd] no card; clips disabled");
    edgeCfg.person_threshold = HEARTH_PERSON_THRESHOLD;
    edgeCfg.retention_days   = HEARTH_RETENTION_DAYS;
    clips.setRetentionDays(edgeCfg.retention_days);

    mdns.begin(deviceId, "camera", HEARTH_DEVICE_NAME);
    mqtt.begin(HEARTH_MQTT_HOST, HEARTH_MQTT_PORT, deviceId, "camera", HEARTH_DEVICE_NAME, FW);
    mqtt.setLocation(HEARTH_LOCATION);
    mqtt.onCommand(onCommand);

    Httpd::Hooks hooks;
    hooks.snapshot = hookSnapshot; hooks.stream = hookStream;
    hooks.onConfig = onConfig;     hooks.statusExtra = statusExtra;
    httpd.begin(deviceId, "camera", HEARTH_DEVICE_NAME, FW, auth, &clips,
                mdns.lanHost(), wifi.softApSsid(), &edgeCfg, hooks, &nowUnix);

    mqtt.loop();
    mqtt.publishAnnounce(httpd.httpBase(), "mqtt", capsJson());
}

static uint32_t lastInfer = 0;
void loop() {
    if (wifi.isProvisioning()) { wifi.loop(); return; }
    httpd.loop();
    mqtt.loop();
    time_t t = time(nullptr); if (t > 1700000000) nowUnix = (long)t;

    if (millis() - lastInfer > 200) {
        lastInfer = millis();
        GroveDet d = grove.poll();
        if (d.person && d.confidence >= edgeCfg.person_threshold) {
            recordAndPublish(d.confidence);
            delay(4000);   // debounce
        }
    }
}
