// cam-esp32s3 — Hearth Budget cam (Seeed XIAO ESP32-S3 Sense).
// ----------------------------------------------------------------------------
// Camera capture -> motion gate -> on-device person detect (ESP-DL / stub) ->
// record clip to microSD -> publish CameraEvent (FABRIC.md §4). Full standalone:
// device HTTP API (snapshot/stream/clips/config/provision/pair) + MQTT + mDNS +
// SoftAP provisioning + HMAC pairing, all from firmware/common.
//
// When the ESP-DL model isn't compiled in (HEARTH_HAVE_ESPDL undefined), the
// detector degrades to motion-only and events are published as kind="motion".

#include <Arduino.h>
#include "esp_camera.h"
#include <SD_MMC.h>
#include <time.h>

#include "config.h"
#include "camera_pins.h"
#include "person_detector.h"

#include "hearth_id.h"
#include "hearth_wifi.h"
#include "hearth_mdns.h"
#include "hearth_mqtt.h"
#include "hearth_auth.h"
#include "hearth_httpd.h"
#include "hearth_sdclip.h"
#include "hearth_ota.h"

static const char* FW = "0.2.0";

using namespace hearth;

static Wifi   wifi;
static Auth   auth;
static Mdns   mdns;
static Mqtt   mqtt;
static Httpd  httpd;
static SdClip clips;
static EdgeConfig edgeCfg;
static EspDlPersonDetector detector;
static long   nowUnix = 0;   // synced clock; 0 until NTP/hub sets it

static String deviceId;

// ---- camera ----------------------------------------------------------------
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
    c.frame_size   = FRAMESIZE_SVGA;   // 800x600; PSRAM on the Sense
    c.jpeg_quality = 12;
    c.fb_count     = 2;
    c.grab_mode    = CAMERA_GRAB_LATEST;
    c.fb_location  = CAMERA_FB_IN_PSRAM;
    esp_err_t err = esp_camera_init(&c);
    if (err != ESP_OK) { Serial.printf("camera init 0x%x\n", err); return false; }
    return true;
}

// ---- HTTP hooks (snapshot/stream/config) -----------------------------------
static bool hookSnapshot(WebServer& s) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) return false;
    s.setContentLength(fb->len);
    s.send(200, "image/jpeg", "");
    s.sendContent((const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return true;
}

static void hookStream(WebServer& s) {
    WiFiClient client = s.client();
    client.print("HTTP/1.1 200 OK\r\n"
                 "Content-Type: multipart/x-mixed-replace;boundary=frame\r\n"
                 "Access-Control-Allow-Origin: *\r\n\r\n");
    while (client.connected()) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) break;
        client.printf("\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
        client.write(fb->buf, fb->len);
        esp_camera_fb_return(fb);
        if (!client.connected()) break;
    }
}

static void onConfig(const EdgeConfig& c) {
    clips.setRetentionDays(c.retention_days);
    Serial.printf("[cfg] thr=%.2f retain=%d face=%d\n",
                  c.person_threshold, c.retention_days, c.face);
}

static String statusExtra() {
    // capabilities mirror for the status page.
    return String("\"capabilities\":{\"stream\":true,\"snapshot\":true,\"clips\":true,"
                  "\"person_detect\":\"trigger\",\"resolution\":\"800x600\",\"sd\":") +
           (clips.ready() ? "true}" : "false}");
}

// ---- MQTT command dispatch (§7) --------------------------------------------
static void onCommand(const String& name, const String& payload) {
    if (name == "config") {
        // {"person_threshold":..,"retention_days":..,"face":..}
        auto num = [&](const char* k, float def)->float{
            int i = payload.indexOf(String("\"")+k+"\"");
            if (i<0) return def; i = payload.indexOf(':', i); if (i<0) return def;
            return payload.substring(i+1).toFloat();
        };
        edgeCfg.person_threshold = num("person_threshold", edgeCfg.person_threshold);
        edgeCfg.retention_days   = (int)num("retention_days", edgeCfg.retention_days);
        edgeCfg.face             = payload.indexOf("\"face\":true") >= 0;
        onConfig(edgeCfg);
    } else if (name == "identify") {
        // blink the orange LED on the XIAO S3 (GPIO21, active-low).
        pinMode(21, OUTPUT);
        for (int i=0;i<6;i++){ digitalWrite(21, i&1); delay(120); }
    } else if (name == "ota") {
        Ota::handle(payload);
    }
}

// ---- capabilities announce (§3) --------------------------------------------
static String capsJson() {
    String pd = detector.name();
    // "reliable" only once the real model is wired; otherwise "trigger".
    const char* level = (pd.indexOf("stub") >= 0 || pd == "motion") ? "trigger" : "reliable";
    String j = "{";
    j += "\"stream\":true,\"snapshot\":true,\"clips\":true,";
    j += String("\"person_detect\":\"") + level + "\",";
    j += "\"resolution\":\"800x600\",\"sd\":" + String(clips.ready() ? "true" : "false");
    j += "}";
    return j;
}

// ---- the detection -> clip -> event pipeline -------------------------------
static String base64Thumb(const camera_fb_t* fb);   // fwd

static void recordAndPublish(const char* evKind, float conf) {
    long ts = (nowUnix > 0) ? nowUnix : (long)(millis() / 1000);
    // Record a short clip: grab ~30 frames.
    String file = clips.beginClip(ts);
    String thumbB64;
    if (file.length()) {
        for (int i = 0; i < 30; ++i) {
            camera_fb_t* fb = esp_camera_fb_get();
            if (!fb) break;
            if (i == 0) thumbB64 = base64Thumb(fb);   // first frame = thumbnail
            clips.addFrame(fb->buf, fb->len);
            esp_camera_fb_return(fb);
        }
        clips.endClip();
    }
    String url = file.length() ? SdClip::clipUrl(httpd.httpBase(), file) : "";
    mqtt.publishEvent(evKind, conf, thumbB64, url, ts);
    Serial.printf("[event] %s conf=%.2f clip=%s\n", evKind, conf, url.c_str());
}

void setup() {
    Serial.begin(115200);
    delay(200);

    auth.begin();                          // per-device key (NVS)
    if (!initCamera()) { delay(3000); ESP.restart(); }

    wifi.begin(Kind::Camera);              // STA or SoftAP provisioning
    if (wifi.isProvisioning()) {           // stay in captive portal until provisioned
        Serial.println("[boot] provisioning mode");
        return;                            // loop() pumps wifi.loop()
    }

    deviceId = hearth::deviceId(Kind::Camera);
    configTime(0, 0, "pool.ntp.org");      // best-effort clock for ts/auth freshness

    // microSD (1-bit SD_MMC on the Sense; pins set by board pinmux).
    SD_MMC.setPins(HEARTH_SD_CLK, HEARTH_SD_CMD, HEARTH_SD_D0);
    if (SD_MMC.begin("/sdcard", true)) clips.begin(SD_MMC);
    else Serial.println("[sd] no card; clips disabled");
    clips.setRetentionDays(edgeCfg.retention_days);

    detector.begin();

    mdns.begin(deviceId, "camera", HEARTH_DEVICE_NAME);

    mqtt.begin(HEARTH_MQTT_HOST, HEARTH_MQTT_PORT, deviceId, "camera",
               HEARTH_DEVICE_NAME, FW);
    mqtt.setLocation(HEARTH_LOCATION);
    mqtt.onCommand(onCommand);

    edgeCfg.person_threshold = HEARTH_PERSON_THRESHOLD;
    edgeCfg.retention_days   = HEARTH_RETENTION_DAYS;

    Httpd::Hooks hooks;
    hooks.snapshot    = hookSnapshot;
    hooks.stream      = hookStream;
    hooks.onConfig    = onConfig;
    hooks.statusExtra = statusExtra;
    httpd.begin(deviceId, "camera", HEARTH_DEVICE_NAME, FW, auth, &clips,
                mdns.lanHost(), wifi.softApSsid(), &edgeCfg, hooks, &nowUnix);

    mqtt.loop();   // connect + birth + announce
    mqtt.publishAnnounce(httpd.httpBase(), "mqtt", capsJson());
}

static uint32_t lastInfer = 0;

void loop() {
    if (wifi.isProvisioning()) { wifi.loop(); return; }

    httpd.loop();
    mqtt.loop();

    time_t t = time(nullptr);
    if (t > 1700000000) nowUnix = (long)t;   // clock synced

    // Run detection a few times a second; clip+event when person/motion fires.
    if (millis() - lastInfer > 250) {
        lastInfer = millis();
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) {
            Detection d = detector.detect(fb);
            esp_camera_fb_return(fb);
            String dn = detector.name();
            bool modelLive = (dn.indexOf("stub") < 0 && dn != "motion");
            if (modelLive && d.person && d.confidence >= edgeCfg.person_threshold) {
                recordAndPublish("person", d.confidence);
                delay(4000);   // simple debounce between events
            } else if (!modelLive && d.confidence >= 0.04f) {
                recordAndPublish("motion", d.confidence);   // motion-only fallback
                delay(4000);
            }
        }
    }
}

// Tiny base64 of a downscaled JPEG would need a re-encode; for the thumbnail we
// base64 the full first JPEG frame (already <=480px-ish at SVGA quality budget).
// FABRIC.md §4 caps thumb at <=480px — TODO(thumb): re-scale to <=480px to shrink
// the MQTT payload; SVGA frames may exceed the broker buffer on busy networks.
#include <mbedtls/base64.h>
static String base64Thumb(const camera_fb_t* fb) {
    if (!fb) return "";
    size_t cap = ((fb->len + 2) / 3) * 4 + 4;
    if (cap > 60000) return "";   // guard the MQTT buffer
    String out; out.reserve(cap);
    uint8_t* tmp = (uint8_t*)malloc(cap);
    if (!tmp) return "";
    size_t olen = 0;
    if (mbedtls_base64_encode(tmp, cap, &olen, fb->buf, fb->len) == 0)
        out = String((const char*)tmp, olen);
    free(tmp);
    return out;
}
