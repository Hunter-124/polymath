// voice-sat — Hearth voice satellite (ESP32-S3 + I2S mic/amp).
// ----------------------------------------------------------------------------
// On-device wake gate (microWakeWord, stub-fallback) -> stream 16 kHz mono audio
// to hub:8770 over UDP with the FABRIC.md §8 4-byte header. Posts wake events to
// hearth/<id>/wake and plays TTS from cmd/tts. Reuses firmware/common for
// wifi/provisioning/mqtt/id/auth.
//
//   datagram = [uint16 device_seq LE][uint8 codec][uint8 room_id] payload...
//   codec 0 = PCM16 (always), 1 = Opus (if HEARTH_CODEC_OPUS=1 + libopus built).

#include <Arduino.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <time.h>

#include "config.h"
#include "audio_pins.h"
#include "audio_io.h"
#include "wakeword.h"

#include "hearth_id.h"
#include "hearth_wifi.h"
#include "hearth_mdns.h"
#include "hearth_mqtt.h"
#include "hearth_auth.h"
#include "hearth_ota.h"

static const char* FW = "0.2.0";
using namespace hearth;

static Wifi     wifi;
static Auth     auth;
static Mdns     mdns;
static Mqtt     mqtt;
static AudioIO  audio;
static WakeWord wake;
static WiFiUDP  udp;

static String   deviceId;
static long     nowUnix = 0;
static uint16_t seq = 0;
static bool     streaming = false;
static uint32_t streamUntil = 0;

// Send one audio frame with the §8 header. `samples` is 16 kHz mono int16.
static void sendFrame(const int16_t* samples, size_t n) {
    udp.beginPacket(HEARTH_HUB_AUDIO_HOST, HEARTH_HUB_AUDIO_PORT);
    uint8_t hdr[4];
    hdr[0] = (uint8_t)(seq & 0xff);
    hdr[1] = (uint8_t)(seq >> 8);          // uint16 device_seq, little-endian
    hdr[2] = HEARTH_CODEC_OPUS ? 1 : 0;    // codec
    hdr[3] = (uint8_t)HEARTH_ROOM_ID;      // room_id
    udp.write(hdr, 4);
#if HEARTH_CODEC_OPUS
    // TODO(codec): Opus-encode `samples` (20ms @16k = 320 samples) here. Until
    // libopus is wired in we fall through to PCM16 even when codec=1 is signalled.
    udp.write((const uint8_t*)samples, n * sizeof(int16_t));
#else
    udp.write((const uint8_t*)samples, n * sizeof(int16_t));   // raw PCM16 LE
#endif
    udp.endPacket();
    seq++;
}

// cmd/tts: fetch the WAV and play it. We parse a minimal 16-bit PCM WAV header.
static void playTtsUrl(const String& url) {
    HTTPClient http;
    if (!http.begin(url)) return;
    if (http.GET() != HTTP_CODE_OK) { http.end(); return; }
    WiFiClient* s = http.getStreamPtr();
    // Skip 44-byte canonical WAV header (assumes 16k/mono/16-bit from the hub TTS).
    uint8_t hdr[44]; size_t got = 0;
    while (got < 44 && http.connected()) { int r = s->read(hdr + got, 44 - got); if (r > 0) got += r; else delay(1); }
    int16_t buf[320];
    while (http.connected()) {
        size_t avail = s->available();
        if (!avail) { delay(2); if (!http.connected()) break; continue; }
        int r = s->readBytes((uint8_t*)buf, avail > sizeof(buf) ? sizeof(buf) : avail);
        if (r <= 0) break;
        audio.playPcm(buf, r / sizeof(int16_t));
    }
    http.end();
}

static String jsonStr(const String& body, const char* key) {
    int k = body.indexOf(String("\"") + key + "\"");
    if (k < 0) return ""; int c = body.indexOf(':', k); if (c < 0) return "";
    int q1 = body.indexOf('"', c); if (q1 < 0) return "";
    int q2 = body.indexOf('"', q1 + 1); if (q2 < 0) return "";
    return body.substring(q1 + 1, q2);
}

static void onCommand(const String& name, const String& payload) {
    if (name == "tts") {
        String url = jsonStr(payload, "audio_url");
        if (url.length()) playTtsUrl(url);
    } else if (name == "identify") {
        // chirp: a short 1kHz tone burst.
        int16_t tone[160];
        for (int i = 0; i < 160; ++i) tone[i] = (int16_t)(6000 * sinf(i * 0.39f));
        for (int r = 0; r < 20; ++r) audio.playPcm(tone, 160);
    } else if (name == "ota") {
        Ota::handle(payload);
    }
}

static String capsJson() {
    return String("{\"audio\":true,\"wake\":\"") + wake.name() +
           "\",\"sample_rate\":16000,\"hw\":\"" + HEARTH_HW_NAME + "\"}";
}

void setup() {
    Serial.begin(115200); delay(200);
    auth.begin();

    wifi.begin(Kind::VoiceSat);
    if (wifi.isProvisioning()) { Serial.println("[boot] provisioning"); return; }

    deviceId = hearth::deviceId(Kind::VoiceSat);
    configTime(0, 0, "pool.ntp.org");

    if (!audio.begin()) Serial.println("[audio] I2S init failed");
    wake.begin();

    mdns.begin(deviceId, "voice_sat", HEARTH_DEVICE_NAME);
    mqtt.begin(HEARTH_MQTT_HOST, HEARTH_MQTT_PORT, deviceId, "voice_sat",
               HEARTH_DEVICE_NAME, FW);
    mqtt.setLocation(HEARTH_LOCATION);
    mqtt.onCommand(onCommand);

    mqtt.loop();
    mqtt.publishAnnounce(String("http://") + WiFi.localIP().toString(), "mqtt", capsJson());
    Serial.printf("[boot] sat up: %s  wake=%s  hw=%s\n",
                  deviceId.c_str(), wake.name(), HEARTH_HW_NAME);
}

static int16_t mic[320];   // 20 ms @ 16 kHz

void loop() {
    if (wifi.isProvisioning()) { wifi.loop(); return; }
    mqtt.loop();
    time_t t = time(nullptr); if (t > 1700000000) nowUnix = (long)t;

    size_t n = audio.readMic(mic, 320);
    if (!n) return;

    if (!streaming) {
        if (wake.feed(mic, n)) {
            streaming   = true;
            streamUntil = millis() + 8000;   // stream up to 8s per utterance
            seq = 0;
            mqtt.publishWake(HEARTH_WAKE_PHRASE, nowUnix);
            Serial.println("[wake] streaming");
        }
    } else {
        sendFrame(mic, n);
        // Stop after a fixed window (the hub does endpointing/VAD on its side).
        if (millis() > streamUntil) { streaming = false; Serial.println("[wake] stream end"); }
    }
}
