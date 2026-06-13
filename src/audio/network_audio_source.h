#pragma once
//
// NetworkAudioSource — receives 16 kHz mono audio from Wi-Fi voice satellites
// over UDP (hub:8770 by default) and writes float samples into a FloatRing.
//
// Each datagram carries a 4-byte little-endian header:
//   [uint16 device_seq][uint8 codec (0=pcm16, 1=opus)][uint8 room_id]  payload...
//
// Codec support:
//   * PCM16 (codec=0): always decoded; int16 samples converted to float [-1,1].
//   * Opus  (codec=1): decoded only when built with -DPOLYMATH_USE_OPUS=ON;
//     otherwise datagrams are dropped with a one-time warning.
//
// Threading: owns one std::thread (the receive loop). The FloatRing reference
// must outlive this object. The ring is SPSC — only this object writes; the
// AudioService worker drains (dedicated ring, not shared with Capture).
//
// Source tracking: after each valid datagram the last active room_id is stored
// so AudioService can tag Utterance.source. Access via lastRoomId().
//
#include "audio_common.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace polymath::audio {

class NetworkAudioSource {
public:
    // port: UDP port to bind (default 8770). ring: dedicated FloatRing to write into.
    explicit NetworkAudioSource(FloatRing& ring, uint16_t port = 8770);
    ~NetworkAudioSource();

    // Bind the socket and launch the receive thread. Returns false on bind failure.
    bool start();
    void stop();
    bool isRunning() const;

    // Last room_id byte seen in any valid received datagram (0 if none yet).
    uint8_t lastRoomId() const { return last_room_id_.load(std::memory_order_acquire); }

    // Optional callback: fired (from the rx thread) on each valid datagram with
    // the room_id. AudioService can use this for finer-grained source tagging.
    // Keep the callback fast — it runs on the receive thread.
    void setRoomCallback(std::function<void(uint8_t room_id)> cb);

private:
    void rxLoop();   // runs on thread_

    FloatRing&                       ring_;
    uint16_t                         port_;
    std::atomic<bool>                running_{false};
    std::thread                      thread_;
    uintptr_t                        sock_ = ~uintptr_t{0};  // platform socket fd; ~0 = invalid
    std::atomic<uint8_t>             last_room_id_{0};
    std::function<void(uint8_t)>     room_cb_;
};

} // namespace polymath::audio
