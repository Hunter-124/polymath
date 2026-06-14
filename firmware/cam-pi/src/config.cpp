#include "hearth_pi/config.h"
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace hearth {

// Minimal JSON value extractor (string/number/bool) — avoids a JSON dep for a
// flat config file. Good enough for /etc/hearth/cam-pi.json's one-level object.
static std::string strField(const std::string& j, const std::string& key, const std::string& def) {
    auto k = j.find("\"" + key + "\"");
    if (k == std::string::npos) return def;
    auto colon = j.find(':', k);
    auto q1 = j.find('"', colon);
    if (q1 == std::string::npos) return def;
    auto q2 = j.find('"', q1 + 1);
    if (q2 == std::string::npos) return def;
    return j.substr(q1 + 1, q2 - q1 - 1);
}
static double numField(const std::string& j, const std::string& key, double def) {
    auto k = j.find("\"" + key + "\"");
    if (k == std::string::npos) return def;
    auto colon = j.find(':', k);
    if (colon == std::string::npos) return def;
    return strtod(j.c_str() + colon + 1, nullptr);
}
static bool boolField(const std::string& j, const std::string& key, bool def) {
    auto k = j.find("\"" + key + "\"");
    if (k == std::string::npos) return def;
    auto colon = j.find(':', k);
    if (colon == std::string::npos) return def;
    // look at the token right after the colon
    auto t = j.find_first_not_of(" \t", colon + 1);
    return t != std::string::npos && j.compare(t, 4, "true") == 0;
}

PiConfig PiConfig::load(const std::string& path) {
    PiConfig c;
    std::ifstream in(path);
    if (!in) return c;   // defaults
    std::stringstream ss; ss << in.rdbuf();
    std::string j = ss.str();
    c.device_name = strField(j, "device_name", c.device_name);
    c.location    = strField(j, "location", c.location);
    c.mqtt_host   = strField(j, "mqtt_host", c.mqtt_host);
    c.mqtt_port   = (uint16_t)numField(j, "mqtt_port", c.mqtt_port);
    c.camera      = strField(j, "camera", c.camera);
    c.clips_dir   = strField(j, "clips_dir", c.clips_dir);
    c.key_path    = strField(j, "key_path", c.key_path);
    c.model_path  = strField(j, "model_path", c.model_path);
    c.person_threshold = (float)numField(j, "person_threshold", c.person_threshold);
    c.retention_days   = (int)numField(j, "retention_days", c.retention_days);
    c.http_port   = (uint16_t)numField(j, "http_port", c.http_port);
    c.edge_hub    = boolField(j, "edge_hub", c.edge_hub);
    return c;
}

} // namespace hearth
