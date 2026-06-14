// Polymath ESP32-CAM firmware
// ----------------------------------------------------------------------------
// Streams MJPEG over HTTP for the Polymath VisionService to consume.
// Endpoints (port 80):
//   GET /            -> tiny status/help page
//   GET /stream      -> multipart/x-mixed-replace MJPEG stream  (use this URL)
//   GET /snapshot    -> single JPEG frame
//   GET /status      -> JSON {name, rssi, fps_hint}
//
// Board: "AI Thinker ESP32-CAM" (Tools > Board > ESP32 Arduino).
// Configure Wi-Fi + device name in config.h before flashing. See README.md.

#include "config.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include <WiFi.h>
#include <ESPmDNS.h>

// --- AI-Thinker ESP32-CAM pin map -------------------------------------------
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
static const char* STREAM_BOUNDARY     = "\r\n--frame\r\n";
static const char* STREAM_PART         = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static httpd_handle_t s_httpd = nullptr;

static bool initCamera() {
    camera_config_t c = {};
    c.ledc_channel = LEDC_CHANNEL_0;
    c.ledc_timer   = LEDC_TIMER_0;
    c.pin_d0 = Y2_GPIO_NUM;  c.pin_d1 = Y3_GPIO_NUM;  c.pin_d2 = Y4_GPIO_NUM;  c.pin_d3 = Y5_GPIO_NUM;
    c.pin_d4 = Y6_GPIO_NUM;  c.pin_d5 = Y7_GPIO_NUM;  c.pin_d6 = Y8_GPIO_NUM;  c.pin_d7 = Y9_GPIO_NUM;
    c.pin_xclk = XCLK_GPIO_NUM;  c.pin_pclk = PCLK_GPIO_NUM;  c.pin_vsync = VSYNC_GPIO_NUM;
    c.pin_href = HREF_GPIO_NUM;   c.pin_sccb_sda = SIOD_GPIO_NUM; c.pin_sccb_scl = SIOC_GPIO_NUM;
    c.pin_pwdn = PWDN_GPIO_NUM;   c.pin_reset = RESET_GPIO_NUM;
    c.xclk_freq_hz = 20000000;
    c.pixel_format = PIXFORMAT_JPEG;
    // Higher res when PSRAM is present (most ESP32-CAMs have it).
    if (psramFound()) {
        c.frame_size = FRAMESIZE_SVGA;   // 800x600
        c.jpeg_quality = 12;
        c.fb_count = 2;
        c.grab_mode = CAMERA_GRAB_LATEST;
        c.fb_location = CAMERA_FB_IN_PSRAM;
    } else {
        c.frame_size = FRAMESIZE_VGA;    // 640x480
        c.jpeg_quality = 15;
        c.fb_count = 1;
    }
    esp_err_t err = esp_camera_init(&c);
    if (err != ESP_OK) { Serial.printf("camera init failed: 0x%x\n", err); return false; }
    return true;
}

static esp_err_t snapshotHandler(httpd_req_t* req) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=snap.jpg");
    esp_err_t res = httpd_resp_send(req, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return res;
}

static esp_err_t streamHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char part[64];
    while (true) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) return ESP_FAIL;
        size_t hlen = snprintf(part, sizeof(part), STREAM_PART, fb->len);
        if (httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY)) != ESP_OK ||
            httpd_resp_send_chunk(req, part, hlen) != ESP_OK ||
            httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len) != ESP_OK) {
            esp_camera_fb_return(fb);
            break;   // client disconnected
        }
        esp_camera_fb_return(fb);
    }
    return ESP_OK;
}

static esp_err_t statusHandler(httpd_req_t* req) {
    char body[160];
    snprintf(body, sizeof(body),
             "{\"name\":\"%s\",\"ip\":\"%s\",\"rssi\":%ld}",
             DEVICE_NAME, WiFi.localIP().toString().c_str(), (long)WiFi.RSSI());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, strlen(body));
}

static esp_err_t indexHandler(httpd_req_t* req) {
    const char* html =
        "<h3>Polymath ESP32-CAM</h3>"
        "<p><a href=\"/stream\">/stream</a> (MJPEG) &middot; "
        "<a href=\"/snapshot\">/snapshot</a> &middot; "
        "<a href=\"/status\">/status</a></p>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, strlen(html));
}

static void startServer() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_uri_handlers = 8;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) return;
    httpd_uri_t routes[] = {
        { "/",         HTTP_GET, indexHandler,    nullptr },
        { "/stream",   HTTP_GET, streamHandler,   nullptr },
        { "/snapshot", HTTP_GET, snapshotHandler, nullptr },
        { "/status",   HTTP_GET, statusHandler,   nullptr },
    };
    for (auto& r : routes) httpd_register_uri_handler(s_httpd, &r);
}

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(false);

    if (!initCamera()) { delay(3000); ESP.restart(); }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("Connecting to %s", WIFI_SSID);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) { delay(400); Serial.print("."); }
    if (WiFi.status() != WL_CONNECTED) { Serial.println(" failed; restarting"); ESP.restart(); }

    Serial.printf("\nConnected. Stream:  http://%s/stream\n", WiFi.localIP().toString().c_str());
    if (MDNS.begin(DEVICE_NAME))
        Serial.printf("mDNS:  http://%s.local/stream\n", DEVICE_NAME);

    startServer();
}

void loop() { delay(1000); }   // all work happens in the HTTP server task
