#pragma once
// hearth_auth — per-device HMAC bearer auth + pairing QR payload (FABRIC.md §6).
//
// SCHEME (device side): the mobile app holds {device_id, key} from the pairing QR
// and sends, per request:
//     Authorization: Bearer <base64url( HMAC-SHA256(key, path + "." + ts) )>
//     X-Hearth-Ts: <unix seconds>
// The device recomputes the HMAC over (path + "." + ts) with its own `key` and
// accepts iff it matches and |now - ts| <= skew. This mirrors the hub gateway's
// HMAC-SHA256 language (src/gateway/auth.h) so cameras and hub speak one auth.
//
// `key` is a 32-byte per-device secret generated on first boot and persisted in
// NVS. It is surfaced exactly once via the pairing QR (printed/serial/e-ink).

#include <Arduino.h>
#include "hearth_id.h"

namespace hearth {

class Auth {
public:
    // Loads (or on first boot generates + persists) the 32-byte per-device key.
    void begin();

    // Raw 32-byte key, base64 (standard, padded) — what the QR carries as "key".
    String keyB64() const;

    // Verify an incoming request. `authHeader` is the full "Authorization" value
    // (with or without leading "Bearer "); `path` is the request path WITHOUT the
    // query string; `ts` is the client-supplied X-Hearth-Ts (unix seconds).
    // `nowUnix` is the device's best clock (0 if unsynced -> skew check skipped).
    // Returns true on a valid, fresh signature.
    bool verify(const String& authHeader, const String& path,
                long ts, long nowUnix) const;

    // Compute the expected bearer value for (path, ts) — used by /pair self-test
    // and unit checks. Returns base64url(HMAC).
    String sign(const String& path, long ts) const;

    // Pairing QR JSON payload (FABRIC.md §6). Caller supplies the long kind and
    // the mDNS lan_host + softap ssid.
    String qrPayload(const String& deviceId, const char* kind,
                     const String& softap, const String& lanHost) const;

    // Max allowed |now - ts| in seconds for a request to be considered fresh.
    static constexpr long kSkewSeconds = 120;

private:
    uint8_t key_[32] = {0};
    bool    loaded_  = false;

    void hmac(const uint8_t* msg, size_t len, uint8_t out[32]) const;
};

} // namespace hearth
