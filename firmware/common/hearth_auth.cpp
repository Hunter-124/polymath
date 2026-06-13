#include "hearth_auth.h"
#include <Preferences.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <esp_random.h>

namespace hearth {

static const char* kNs  = "hearth";
static const char* kKey = "devkey";   // 32-byte blob

void Auth::begin() {
    Preferences p;
    p.begin(kNs, /*readOnly=*/false);
    size_t got = p.getBytes(kKey, key_, sizeof(key_));
    if (got != sizeof(key_)) {
        // First boot: generate a 32-byte secret from the hardware RNG.
        esp_fill_random(key_, sizeof(key_));
        p.putBytes(kKey, key_, sizeof(key_));
    }
    p.end();
    loaded_ = true;
}

void Auth::hmac(const uint8_t* msg, size_t len, uint8_t out[32]) const {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_hmac(info, key_, sizeof(key_), msg, len, out);
}

// base64url (no padding) of a digest — matches the hub gateway's b64urlEncode.
static String b64url(const uint8_t* in, size_t len) {
    uint8_t tmp[64];
    size_t olen = 0;
    mbedtls_base64_encode(tmp, sizeof(tmp), &olen, in, len);
    String s((const char*)tmp, olen);
    s.replace('+', '-');
    s.replace('/', '_');
    while (s.length() && s[s.length() - 1] == '=') s.remove(s.length() - 1);
    return s;
}

String Auth::keyB64() const {
    uint8_t tmp[64];
    size_t olen = 0;
    mbedtls_base64_encode(tmp, sizeof(tmp), &olen, key_, sizeof(key_));
    return String((const char*)tmp, olen);   // standard base64, padded
}

String Auth::sign(const String& path, long ts) const {
    String msg = path + "." + String(ts);
    uint8_t out[32];
    hmac((const uint8_t*)msg.c_str(), msg.length(), out);
    return b64url(out, sizeof(out));
}

bool Auth::verify(const String& authHeader, const String& path,
                  long ts, long nowUnix) const {
    if (!loaded_) return false;

    String tok = authHeader;
    tok.trim();
    if (tok.startsWith("Bearer ") || tok.startsWith("bearer "))
        tok = tok.substring(7);
    tok.trim();
    if (!tok.length()) return false;

    // Freshness: only enforced when the device clock is synced (nowUnix > 0).
    if (nowUnix > 0) {
        long delta = nowUnix - ts;
        if (delta < 0) delta = -delta;
        if (delta > kSkewSeconds) return false;
    }

    const String expected = sign(path, ts);
    if (expected.length() != tok.length()) return false;

    // Constant-time compare (XOR-accumulate; both are fixed-length b64url digests).
    uint8_t diff = 0;
    for (size_t i = 0; i < (size_t)expected.length(); ++i)
        diff |= (uint8_t)(expected[i] ^ tok[i]);
    return diff == 0;
}

String Auth::qrPayload(const String& deviceId, const char* kind,
                       const String& softap, const String& lanHost) const {
    // FABRIC.md §6 exact field order/names.
    String j = "{";
    j += "\"v\":1,";
    j += "\"device_id\":\"" + deviceId + "\",";
    j += "\"kind\":\""      + String(kind) + "\",";
    j += "\"key\":\""       + keyB64() + "\",";
    j += "\"softap\":\""    + softap + "\",";
    j += "\"lan_host\":\""  + lanHost + "\"";
    j += "}";
    return j;
}

} // namespace hearth
