#include "hearth_pi/auth.h"
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <filesystem>

namespace hearth {
namespace fs = std::filesystem;

static std::string b64(const uint8_t* d, size_t n, bool url) {
    static const char* std_a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static const char* url_a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const char* a = url ? url_a : std_a;
    std::string out;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = d[i] << 16;
        if (i + 1 < n) v |= d[i + 1] << 8;
        if (i + 2 < n) v |= d[i + 2];
        out += a[(v >> 18) & 63];
        out += a[(v >> 12) & 63];
        out += (i + 1 < n) ? a[(v >> 6) & 63] : (url ? '\0' : '=');
        out += (i + 2 < n) ? a[v & 63]        : (url ? '\0' : '=');
    }
    if (url) { std::string s; for (char c : out) if (c) s += c; return s; }
    return out;
}

Auth::Auth(std::string keyPath) {
    std::ifstream in(keyPath, std::ios::binary);
    if (in && in.read((char*)key_.data(), key_.size()) && in.gcount() == (std::streamsize)key_.size())
        return;
    // generate + persist
    RAND_bytes(key_.data(), key_.size());
    fs::create_directories(fs::path(keyPath).parent_path());
    std::ofstream out(keyPath, std::ios::binary);
    out.write((const char*)key_.data(), key_.size());
}

std::string Auth::keyB64() const { return b64(key_.data(), key_.size(), false); }

std::string Auth::sign(const std::string& path, long ts) const {
    std::string msg = path + "." + std::to_string(ts);
    uint8_t mac[EVP_MAX_MD_SIZE]; unsigned int len = 0;
    HMAC(EVP_sha256(), key_.data(), key_.size(),
         (const uint8_t*)msg.data(), msg.size(), mac, &len);
    return b64(mac, len, true);
}

bool Auth::verify(const std::string& authHeader, const std::string& path,
                  long ts, long nowUnix, long skew) const {
    std::string tok = authHeader;
    if (tok.rfind("Bearer ", 0) == 0 || tok.rfind("bearer ", 0) == 0) tok = tok.substr(7);
    while (!tok.empty() && (tok.front() == ' ')) tok.erase(tok.begin());
    while (!tok.empty() && (tok.back() == ' ' || tok.back() == '\r')) tok.pop_back();
    if (tok.empty()) return false;
    if (nowUnix > 0 && std::abs(nowUnix - ts) > skew) return false;
    std::string exp = sign(path, ts);
    if (exp.size() != tok.size()) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < exp.size(); ++i) diff |= (uint8_t)(exp[i] ^ tok[i]);
    return diff == 0;
}

std::string Auth::qrPayload(const std::string& deviceId, const std::string& kind,
                            const std::string& softap, const std::string& lanHost) const {
    return std::string("{\"v\":1,\"device_id\":\"") + deviceId + "\",\"kind\":\"" + kind +
           "\",\"key\":\"" + keyB64() + "\",\"softap\":\"" + softap +
           "\",\"lan_host\":\"" + lanHost + "\"}";
}

} // namespace hearth
