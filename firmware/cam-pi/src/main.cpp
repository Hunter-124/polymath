// cam-pi — Hearth Flagship cam (Raspberry Pi 5, optional Hailo AI HAT).
// ----------------------------------------------------------------------------
// A Linux service: device HTTP API (§6) + MQTT (§2-§4,§7) + Frigate-style local
// NVR (clip recording on person) using the same ONNX/YOLO approach as the hub.
// Optional --edge-hub mode runs detection on behalf of Budget cams that POST
// frames to /edge/detect. Conforms to docs/FABRIC.md.
//
// Camera capture is abstracted (captureFrame): the shipping build uses a
// documented synthetic-frame stub so the service builds + runs without libcamera/
// OpenCV. Wire real V4L2/libcamera capture where marked TODO(capture); the rest of
// the pipeline (detect -> clip -> event -> HTTP/MQTT) is complete.

#include "hearth_pi/config.h"
#include "hearth_pi/auth.h"
#include "hearth_pi/mqtt.h"
#include "hearth_pi/httpd.h"
#include "hearth_pi/detector.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace hearth;
namespace fs = std::filesystem;
static const char* FW = "0.2.0";

static long nowUnix() { return (long)std::time(nullptr); }

// device_id = hearth-cam-<hex6> from the first non-loopback MAC.
static std::string deviceId() {
    for (const char* iface : {"eth0", "wlan0", "end0"}) {
        std::ifstream f(std::string("/sys/class/net/") + iface + "/address");
        std::string mac; if (f && std::getline(f, mac) && mac.size() >= 17) {
            // mac = aa:bb:cc:dd:ee:ff -> last 3 octets
            std::string hex = mac.substr(9, 2) + mac.substr(12, 2) + mac.substr(15, 2);
            for (auto& c : hex) c = tolower(c);
            return "hearth-cam-" + hex;
        }
    }
    return "hearth-cam-000000";
}

static std::string localIp() {
    struct ifaddrs* ifs = nullptr; std::string ip = "127.0.0.1";
    if (getifaddrs(&ifs) == 0) {
        for (auto* a = ifs; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
            if (std::string(a->ifa_name) == "lo") continue;
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &((sockaddr_in*)a->ifa_addr)->sin_addr, buf, sizeof(buf));
            ip = buf; break;
        }
        freeifaddrs(ifs);
    }
    return ip;
}

// --- camera capture (stub; replace with libcamera/V4L2) ---------------------
static std::vector<uint8_t> g_frameRgb;
static int g_w = 640, g_h = 480;
static bool captureFrame(Frame& out) {
    // TODO(capture): grab a real frame from cfg.camera via libcamera or V4L2 and
    // fill g_frameRgb (RGB888). Here we synthesize a moving gradient so the
    // pipeline + motion stub run end-to-end without a camera attached.
    g_frameRgb.assign((size_t)g_w * g_h * 3, 0);
    static int t = 0; t++;
    for (int y = 0; y < g_h; ++y)
        for (int x = 0; x < g_w; ++x) {
            size_t i = ((size_t)y * g_w + x) * 3;
            g_frameRgb[i + 0] = (uint8_t)((x + t) & 0xff);
            g_frameRgb[i + 1] = (uint8_t)((y * 2) & 0xff);
            g_frameRgb[i + 2] = (uint8_t)((x ^ y) & 0xff);
        }
    out.width = g_w; out.height = g_h; out.rgb = g_frameRgb.data();
    return true;
}

// A real build re-encodes to JPEG; the stub returns a tiny placeholder.
static std::string encodeJpegStub() {
    // TODO(capture): JPEG-encode the current frame (libjpeg/turbojpeg). Stubbed.
    return std::string();   // empty -> /snapshot returns 503 until wired
}

// --- clip recording (Frigate-style) -----------------------------------------
static std::string recordClip(const PiConfig& cfg, long ts) {
    fs::create_directories(cfg.clips_dir);
    std::string fname = std::to_string(ts) + ".mp4";
    // TODO(capture): mux ~5s of H.264/MJPEG to clips_dir/fname (ffmpeg/libav or a
    // V4L2 hardware encoder on the Pi 5). Here we write a placeholder so the
    // clip_url + listing path is exercised.
    std::ofstream out(cfg.clips_dir + "/" + fname, std::ios::binary);
    out << "HEARTH_CLIP_PLACEHOLDER ts=" << ts << "\n";
    return fname;
}
static void prune(const PiConfig& cfg) {
    long cutoff = nowUnix() - (long)cfg.retention_days * 86400;
    if (!fs::exists(cfg.clips_dir)) return;
    for (auto& e : fs::directory_iterator(cfg.clips_dir)) {
        std::string n = e.path().stem().string();
        long ts = strtol(n.c_str(), nullptr, 10);
        if (ts && ts < cutoff) fs::remove(e.path());
    }
}
static std::string clipsListJson(const PiConfig& cfg) {
    std::string out = "[";
    if (fs::exists(cfg.clips_dir)) {
        bool first = true;
        for (auto& e : fs::directory_iterator(cfg.clips_dir)) {
            std::string fn = e.path().filename().string();
            long ts = strtol(e.path().stem().string().c_str(), nullptr, 10);
            if (!first) out += ","; first = false;
            out += "{\"file\":\"" + fn + "\",\"ts\":" + std::to_string(ts) +
                   ",\"size\":" + std::to_string((long)fs::file_size(e.path())) + "}";
        }
    }
    return out + "]";
}

int main(int argc, char** argv) {
    std::string cfgPath = "/etc/hearth/cam-pi.json";
    bool edgeHub = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) cfgPath = argv[++i];
        else if (a == "--edge-hub") edgeHub = true;
    }
    PiConfig cfg = PiConfig::load(cfgPath);
    if (edgeHub) cfg.edge_hub = true;

    std::string did = deviceId();
    std::string ip = localIp();
    std::string httpBase = "http://" + ip;
    std::string lanHost = did + ".local";

    Auth auth(cfg.key_path);
    PersonDetector detector(cfg.model_path);
    detector.begin();
    fprintf(stderr, "[boot] %s  detector=%s  edge_hub=%d\n",
            did.c_str(), detector.backend(), cfg.edge_hub);

    bool modelLive = std::string(detector.backend()).find("stub") == std::string::npos;

    Mqtt mqtt(cfg.mqtt_host, cfg.mqtt_port, did, "camera", cfg.device_name, FW);
    mqtt.setLocation(cfg.location);
    std::atomic<float> threshold{cfg.person_threshold};
    mqtt.onCommand([&](const std::string& name, const std::string& payload) {
        if (name == "config") {
            auto p = payload.find("person_threshold");
            if (p != std::string::npos) {
                auto c = payload.find(':', p);
                if (c != std::string::npos) threshold = (float)strtod(payload.c_str() + c + 1, nullptr);
            }
        } else if (name == "ota") {
            fprintf(stderr, "[ota] %s (handled by apt/systemd on the Pi, not in-process)\n", payload.c_str());
        }
    });
    mqtt.connect();
    std::string level = modelLive ? "reliable" : "trigger";
    std::string caps = "{\"stream\":false,\"snapshot\":true,\"clips\":true,\"person_detect\":\"" +
                       level + "\",\"resolution\":\"640x480\",\"sd\":true}";
    mqtt.publishAnnounce(httpBase, caps);

    // HTTP server in a background thread.
    Httpd::Hooks hooks;
    hooks.snapshotJpeg = [] { return encodeJpegStub(); };
    hooks.clipsListJson = [&] { return clipsListJson(cfg); };
    hooks.clipFile = [&](const std::string& f) -> std::string {
        std::ifstream in(cfg.clips_dir + "/" + f, std::ios::binary);
        if (!in) return "";
        std::stringstream ss; ss << in.rdbuf(); return ss.str();
    };
    hooks.statusExtra = [&] { return "\"capabilities\":" + caps; };
    hooks.configJson = [&] {
        char b[128]; snprintf(b, sizeof(b),
            "{\"person_threshold\":%.2f,\"retention_days\":%d,\"face\":false}",
            threshold.load(), cfg.retention_days);
        return std::string(b);
    };
    hooks.onConfig = [&](const std::string& body) {
        auto p = body.find("person_threshold");
        if (p != std::string::npos) { auto c = body.find(':', p);
            if (c != std::string::npos) threshold = (float)strtod(body.c_str() + c + 1, nullptr); }
    };
    if (cfg.edge_hub) {
        // POST /edge/detect: a Budget cam pushes a frame; we run detection and
        // return a CameraEvent-shaped verdict it can publish as its own.
        hooks.edgeDetect = [&](const std::string& body) -> std::string {
            // TODO(capture): decode the posted JPEG (body) to RGB and run detect().
            // Returns the verdict; the caller stamps its own device_id/clip_url.
            (void)body;
            return "{\"kind\":\"person\",\"confidence\":0.0,\"note\":\"edge-hub stub\"}";
        };
    }
    Httpd httpd(cfg.http_port, did, "camera", cfg.device_name, FW, auth,
                lanHost, "", nowUnix, hooks);
    std::thread httpThread([&] { httpd.run(); });
    httpThread.detach();

    // Detection loop.
    Frame frame;
    auto lastEvent = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    for (;;) {
        if (captureFrame(frame)) {
            Detection d = detector.detect(frame);
            auto now = std::chrono::steady_clock::now();
            bool cooled = std::chrono::duration_cast<std::chrono::seconds>(now - lastEvent).count() >= 4;
            if (cooled) {
                if (modelLive && d.person && d.confidence >= threshold.load()) {
                    long ts = nowUnix(); std::string f = recordClip(cfg, ts); prune(cfg);
                    mqtt.publishEvent("person", d.confidence, "", httpBase + "/clips/" + f, ts);
                    lastEvent = now;
                } else if (!modelLive && d.confidence >= 0.04f) {
                    long ts = nowUnix(); std::string f = recordClip(cfg, ts); prune(cfg);
                    mqtt.publishEvent("motion", d.confidence, "", httpBase + "/clips/" + f, ts);
                    lastEvent = now;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}
