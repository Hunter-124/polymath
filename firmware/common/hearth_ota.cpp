#include "hearth_ota.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <mbedtls/sha256.h>

namespace hearth {

// Tiny JSON string extractor for "key":"value" (avoids an ArduinoJson dep here).
static String jsonStr(const String& body, const char* key) {
    String pat = String("\"") + key + "\"";
    int k = body.indexOf(pat);
    if (k < 0) return "";
    int colon = body.indexOf(':', k);
    if (colon < 0) return "";
    int q1 = body.indexOf('"', colon);
    if (q1 < 0) return "";
    int q2 = body.indexOf('"', q1 + 1);
    if (q2 < 0) return "";
    return body.substring(q1 + 1, q2);
}

static String toHex(const uint8_t* d, size_t n) {
    static const char* h = "0123456789abcdef";
    String s; s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) { s += h[d[i] >> 4]; s += h[d[i] & 0xf]; }
    return s;
}

bool Ota::fetchVerifyFlash(const String& url, const String& sha256Hex) {
    WiFiClientSecure tls;
    tls.setInsecure();   // TODO(security): pin the Hearth update CA / leaf cert.

    HTTPClient http;
    if (!http.begin(tls, url)) { Serial.println("[ota] begin failed"); return false; }
    int code = http.GET();
    if (code != HTTP_CODE_OK) { Serial.printf("[ota] HTTP %d\n", code); http.end(); return false; }

    int len = http.getSize();
    if (len <= 0) { Serial.println("[ota] unknown length"); http.end(); return false; }
    if (!Update.begin(len)) { Serial.println("[ota] no space"); http.end(); return false; }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    int remaining = len;
    while (http.connected() && remaining > 0) {
        size_t avail = stream->available();
        if (!avail) { delay(1); continue; }
        int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
        if (n <= 0) break;
        mbedtls_sha256_update(&sha, buf, n);
        if (Update.write(buf, n) != (size_t)n) { Update.abort(); http.end(); return false; }
        remaining -= n;
    }
    http.end();

    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);

    String got = toHex(digest, 32);
    String want = sha256Hex; want.toLowerCase();
    if (remaining != 0 || got != want) {
        Serial.printf("[ota] verify failed got=%s want=%s\n", got.c_str(), want.c_str());
        Update.abort();
        return false;
    }
    if (!Update.end(true)) { Serial.println("[ota] end failed"); return false; }

    Serial.println("[ota] flashed; rebooting");
    delay(200);
    ESP.restart();
    return true;
}

bool Ota::handle(const String& payload) {
    String url = jsonStr(payload, "url");
    String sha = jsonStr(payload, "sha256");
    if (!url.length() || sha.length() != 64) { Serial.println("[ota] bad cmd"); return false; }
    return fetchVerifyFlash(url, sha);
}

} // namespace hearth
