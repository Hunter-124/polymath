#pragma once
// Auth — per-device HMAC bearer + pairing QR (FABRIC.md §6), matching the ESP and
// K230 firmware and the hub gateway. Bearer = base64url(HMAC-SHA256(key, path+"."+ts)).

#include <array>
#include <cstdint>
#include <string>

namespace hearth {

class Auth {
public:
    // Load (or generate + persist) the 32-byte key at keyPath.
    explicit Auth(std::string keyPath);

    std::string keyB64() const;                       // standard base64 (QR "key")
    std::string sign(const std::string& path, long ts) const;  // base64url(HMAC)
    bool verify(const std::string& authHeader, const std::string& path,
                long ts, long nowUnix, long skew = 120) const;
    std::string qrPayload(const std::string& deviceId, const std::string& kind,
                          const std::string& softap, const std::string& lanHost) const;
private:
    std::array<uint8_t, 32> key_{};
};

} // namespace hearth
