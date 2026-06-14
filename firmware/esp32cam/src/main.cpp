// esp32cam — Hearth Legacy cam (AI-Thinker ESP32-CAM), upgraded.
// ----------------------------------------------------------------------------
// Keeps the original MJPEG stream and ADDS the Hearth fabric: device_id, SoftAP
// provisioning, MQTT announce/presence/event, SD clip-on-motion, and the device
// HTTP API — all from firmware/common. Detection is MOTION-ONLY on this tier;
// reliable person detection needs the S3 / Grove cams (announce person_detect
// reports "trigger", and events publish kind:"motion").
//
// Constraints (AI-Thinker): only ~520KB RAM, PSRAM present on most modules.
// microSD uses SD_MMC 1-bit (shares GPIO4 flash-LED line); clips are optional —
// if no card, the device still streams + reports motion events without a clip_url.

#include <Arduino.h>
#include "esp_camera.h"
#include <SD_MMC.h>
#include <time.h>

#include "config.h"
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

// --- AI-Thinker ESP32-CAM pin map (unchanged from the original firmware) ----
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

static Wifi   wifi;
static Auth   auth;
static Mdns   mdns;
static Mqtt   mqtt;
static Httpd  httpd;
static SdClip clips;
static EdgeConfig edgeCfg;
static long   nowUnix = 0;
static String deviceId;

// coarse motion gate over the JPEG byte stream (same approach as the S3 tier)
static uint8_t s_prev[32 * 24];
static bool    s_havePrev = false;

static bool initCamera() {
    camera_config_t c = {};
    c.ledc_channel = LEDC_CHANNEL_0; c.ledc_timer = LEDC_TIMER_0;
    c.pin_d0 = Y2_GPIO_NUM; c.pin_d1 = Y3_GPIO_NUM; c.pin_d2 = Y4_GPIO_NUM; c.pin_d3 = Y5_GPIO_NUM;
    c.pin_d4 = Y6_GPIO_NUM; c.pin_d5 = Y7_GPIO_NUM; c.pin_d6 = Y8_GPIO_NUM; c.pin_d7 = Y9_GPIO_NUM;
    c.pin_xclk = XCLK_GPIO_NUM; c.pin_pclk = PCLK_GPIO_NUM; c.pin_vsync = VSYNC_GPIO_NUM;
    c.pin_href = HREF_GPIO_NUM; c.pin_sccb_sda = SIOD_GPIO_NUM; c.pin_sccb_scl = SIOC_GPIO_NUM;
    c.pin_pwdn = PWDN_GPIO_NUM; c.pin_reset = RESET_GPIO_NUM;
    c.xclk_freq_hz = 20000000;
    c.pixel_format = PIXFORMAT_JPEG;
    if (psramFound()) { c.frame_size = FRAMESIZE_SVGA; c.jpeg_quality = 12; c.fb_count = 2;
                        c.grab_mode = CAMERA_GRAB_LATEST; c.fb_location = CAMERA_FB_IN_PSRAM; }
    else              { c.frame_size = FRAMESIZE_VGA;  c.jpeg_quality = 15; c.fb_count = 1; }
    esp_err_t err = esp_camera_init(&c);
    if (err != ESP_OK) { Serial.printf("camera init 0x%x\n", err); return false; }
    return true;
}

// returns motion fraction 0..1
static float motionScore(const camera_fb_t* fb) {
    if (!fb || fb->len < sizeof(s_prev)) return 0;
    uint8_t cur[32 * 24];
    size_t stride = fb->len / (32 * 24); if (!stride) stride = 1;
    for (int i = 0; i < 32 * 24; ++i) { size_t o = (size_t)i * stride; cur[i] = o < fb->len ? fb->buf[o] : 0; }
    if (!s_havePrev) { memcpy(s_prev, cur, sizeof(cur)); s_havePrev = true; return 0; }
    int changed = 0;
    for (int i = 0; i < 32 * 24; ++i) if (abs((int)cur[i] - (int)s_prev[i]) > 24) changed++;
    memcpy(s_prev, cur, sizeof(cur));
    return (float)changed / (32.0f * 24.0f);
}

static bool hookSnapshot(WebServer& s) {
    camera_fb_t* fb = esp_camera_fb_get(); if (!fb) return false;
    s.setContentLength(fb->len); s.send(200, "image/jpeg", "");
    s.sendContent((const char*)fb->buf, fb->len); esp_camera_fb_return(fb); return true;
}
static void hookStream(WebServer& s) {
    WiFiClient client = s.client();
    client.print("HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace;boundary=frame\r\n"
                 "Access-Control-Allow-Origin: *\r\n\r\n");
    while (client.connected()) {
        camera_fb_t* fb = esp_camera_fb_get(); if (!fb) break;
        client.printf("\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
        client.write(fb->buf, fb->len); esp_camera_fb_return(fb);
    }
}
static void onConfig(const EdgeConfig& c) { clips.setRetentionDays(c.retention_days); }
static String capsJson() {
    // legacy tier: motion-only -> person_detect:"trigger"
    return String("{\"stream\":true,\"snapshot\":true,\"clips\":") +
           (clips.ready() ? "true" : "false") +
           ",\"person_detect\":\"trigger\",\"resolution\":\"800x600\",\"sd\":" +
           (clips.ready() ? "true}" : "false}");
}
static String statusExtra() { return String("\"capabilities\":") + capsJson(); }

static void onCommand(const String& name, const String& payload) {
    if (name == "config") {
        int i = payload.indexOf("\"retention_days\"");
        if (i >= 0) { i = payload.indexOf(':', i); if (i>=0) { edgeCfg.retention_days = payload.substring(i+1).toInt(); onConfig(edgeCfg); } }
    } else if (name == "identify") {
        pinMode(4, OUTPUT); for (int i=0;i<6;i++){ digitalWrite(4, i&1); delay(120);} digitalWrite(4, LOW);
    } else if (name == "ota") {
        Ota::handle(payload);
    }
}

static String b64(const uint8_t* d, size_t n) {
    if (!d || !n) return "";
    size_t cap = ((n + 2) / 3) * 4 + 4; if (cap > 50000) return "";
    uint8_t* tmp = (uint8_t*)malloc(cap); if (!tmp) return "";
    size_t olen = 0; String out;
    if (mbedtls_base64_encode(tmp, cap, &olen, d, n) == 0) out = String((const char*)tmp, olen);
    free(tmp); return out;
}

static void recordAndPublish(float conf) {
    long ts = (nowUnix > 0) ? nowUnix : (long)(millis() / 1000);
    String file, thumb;
    if (clips.ready()) {
        file = clips.beginClip(ts);
        if (file.length()) {
            for (int i = 0; i < 24; ++i) {
                camera_fb_t* fb = esp_camera_fb_get(); if (!fb) break;
                if (i == 0) thumb = b64(fb->buf, fb->len);
                clips.addFrame(fb->buf, fb->len); esp_camera_fb_return(fb);
            }
            clips.endClip();
        }
    } else {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) { thumb = b64(fb->buf, fb->len); esp_camera_fb_return(fb); }
    }
    String url = file.length() ? SdClip::clipUrl(httpd.httpBase(), file) : "";
    mqtt.publishEvent("motion", conf, thumb, url, ts);   // motion-only tier
    Serial.printf("[event] motion conf=%.2f clip=%s\n", conf, url.c_str());
}

void setup() {
    Serial.begin(115200); Serial.setDebugOutput(false);
    auth.begin();
    if (!initCamera()) { delay(3000); ESP.restart(); }

    wifi.begin(Kind::Camera);
    if (wifi.isProvisioning()) { Serial.println("[boot] provisioning"); return; }

    deviceId = hearth::deviceId(Kind::Camera);
    configTime(0, 0, "pool.ntp.org");

    // SD_MMC 1-bit (AI-Thinker shares pins; many modules need the card seated at boot).
    if (SD_MMC.begin("/sdcard", true)) clips.begin(SD_MMC);
    else Serial.println("[sd] no card; motion events will have no clip_url");
    edgeCfg.retention_days = HEARTH_RETENTION_DAYS;
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

static uint32_t lastCheck = 0;
void loop() {
    if (wifi.isProvisioning()) { wifi.loop(); return; }
    httpd.loop();
    mqtt.loop();
    time_t t = time(nullptr); if (t > 1700000000) nowUnix = (long)t;

    if (millis() - lastCheck > 300) {
        lastCheck = millis();
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) {
            float m = motionScore(fb);
            esp_camera_fb_return(fb);
            if (m >= HEARTH_MOTION_FRAC) { recordAndPublish(m); delay(4000); /*debounce*/ }
        }
    }
}
