#include "network_audio_source.h"
#include "logging.h"

#include <cstring>
#include <vector>

// Platform sockets.
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   using SockLen  = int;
   using SocketFd = SOCKET;
   static constexpr uintptr_t kInvalidSockVal = static_cast<uintptr_t>(INVALID_SOCKET);
   static void closeSock(SocketFd s) { closesocket(s); }
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <unistd.h>
   using SockLen  = socklen_t;
   using SocketFd = int;
   static constexpr uintptr_t kInvalidSockVal = static_cast<uintptr_t>(-1);
   static void closeSock(SocketFd s) { close(s); }
#endif

static inline SocketFd toFd(uintptr_t v)    { return static_cast<SocketFd>(v); }
static inline bool     fdInvalid(uintptr_t v){ return v == kInvalidSockVal; }

#ifdef POLYMATH_USE_OPUS
#  include <opus/opus.h>
#endif

namespace polymath::audio {

// Header layout (4 bytes, little-endian). Use pragma pack for MSVC + GCC/Clang compat.
#pragma pack(push, 1)
struct DgramHeader {
    uint16_t device_seq;
    uint8_t  codec;    // 0=pcm16, 1=opus
    uint8_t  room_id;
};
#pragma pack(pop)
static_assert(sizeof(DgramHeader) == 4);

static constexpr uint8_t kCodecPcm16 = 0;
static constexpr uint8_t kCodecOpus  = 1;

// Max expected datagram size: 20 ms Opus frame at 24 kbps is ~60 bytes; PCM16
// 20 ms at 16 kHz is 640 bytes. Give generous headroom.
static constexpr size_t kMaxDgramBytes = 8192;

// ---- Opus optional decoder --------------------------------------------------
#ifdef POLYMATH_USE_OPUS
// 20 ms frame at 16 kHz = 320 samples.
static constexpr int kOpusFrameSamples = 320;

static OpusDecoder* g_opus_dec = nullptr;

static bool ensureOpusDecoder() {
    if (g_opus_dec) return true;
    int err = 0;
    g_opus_dec = opus_decoder_create(kSampleRate, kChannels, &err);
    if (err != OPUS_OK || !g_opus_dec) {
        PM_ERROR("audio.net: opus_decoder_create failed: {}", opus_strerror(err));
        g_opus_dec = nullptr;
        return false;
    }
    return true;
}

static void destroyOpusDecoder() {
    if (g_opus_dec) { opus_decoder_destroy(g_opus_dec); g_opus_dec = nullptr; }
}
#endif // POLYMATH_USE_OPUS

// ---- NetworkAudioSource -----------------------------------------------------

NetworkAudioSource::NetworkAudioSource(FloatRing& ring, uint16_t port)
    : ring_(ring), port_(port), sock_(kInvalidSockVal) {}

NetworkAudioSource::~NetworkAudioSource() {
    stop();
#ifdef POLYMATH_USE_OPUS
    destroyOpusDecoder();
#endif
}

void NetworkAudioSource::setRoomCallback(std::function<void(uint8_t)> cb) {
    room_cb_ = std::move(cb);
}

bool NetworkAudioSource::start() {
    if (running_.load(std::memory_order_acquire)) return true;

#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        PM_ERROR("audio.net: WSAStartup failed");
        return false;
    }
#endif

    SocketFd s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fdInvalid(static_cast<uintptr_t>(s))) {
        PM_ERROR("audio.net: socket() failed");
        return false;
    }

    // Allow rapid restart without TIME_WAIT blocking rebind.
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);

    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        PM_ERROR("audio.net: bind() on port {} failed", port_);
        closeSock(s);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    // Set a 200 ms receive timeout so the thread can check running_ and exit cleanly.
#ifdef _WIN32
    DWORD tv = 200;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval tv{ .tv_sec = 0, .tv_usec = 200000 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif

    sock_ = static_cast<uintptr_t>(s);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&NetworkAudioSource::rxLoop, this);
    PM_INFO("audio.net: listening on UDP port {}", port_);
    return true;
}

void NetworkAudioSource::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;
    if (thread_.joinable()) thread_.join();
    if (!fdInvalid(sock_)) {
        closeSock(toFd(sock_));
        sock_ = kInvalidSockVal;
    }
#ifdef _WIN32
    WSACleanup();
#endif
    PM_INFO("audio.net: stopped");
}

bool NetworkAudioSource::isRunning() const {
    return running_.load(std::memory_order_acquire);
}

void NetworkAudioSource::rxLoop() {
    std::vector<uint8_t> buf(kMaxDgramBytes);
    // Scratch float buffer for PCM16->float conversion.
    std::vector<float>   samples;
    // One-time Opus-not-supported warning.
    bool warned_opus = false;

    while (running_.load(std::memory_order_acquire)) {
        sockaddr_in src{};
        SockLen src_len = sizeof(src);
        const int n = static_cast<int>(
            recvfrom(toFd(sock_),
                     reinterpret_cast<char*>(buf.data()),
                     static_cast<int>(buf.size()), 0,
                     reinterpret_cast<sockaddr*>(&src), &src_len));

        if (n <= 0) continue;   // timeout or error; check running_ next iteration

        if (n < static_cast<int>(sizeof(DgramHeader))) {
            PM_WARN("audio.net: short datagram ({} bytes), discarded", n);
            continue;
        }

        DgramHeader hdr;
        std::memcpy(&hdr, buf.data(), sizeof(hdr));
        // Header is little-endian; on little-endian hosts this is a no-op.
        // device_seq is informational (future gap detection); not used here.

        const uint8_t* payload     = buf.data() + sizeof(hdr);
        const int      payload_len = n - static_cast<int>(sizeof(hdr));

        if (payload_len <= 0) continue;

        if (hdr.codec == kCodecPcm16) {
            // Expect an even number of bytes for int16 samples.
            if (payload_len % 2 != 0) {
                PM_WARN("audio.net: PCM16 datagram has odd byte count ({}), discarded",
                        payload_len);
                continue;
            }
            const int n_samples = payload_len / 2;
            samples.resize(static_cast<size_t>(n_samples));
            const auto* s16 = reinterpret_cast<const int16_t*>(payload);
            for (int i = 0; i < n_samples; ++i)
                samples[i] = static_cast<float>(s16[i]) / kSampleScale;
            ring_.write(samples.data(), samples.size());

        } else if (hdr.codec == kCodecOpus) {
#ifdef POLYMATH_USE_OPUS
            if (!ensureOpusDecoder()) continue;
            samples.resize(kOpusFrameSamples);
            const int decoded = opus_decode_float(
                g_opus_dec, payload, payload_len,
                samples.data(), kOpusFrameSamples, 0);
            if (decoded < 0) {
                PM_WARN("audio.net: opus_decode_float error: {}", opus_strerror(decoded));
                continue;
            }
            ring_.write(samples.data(), static_cast<size_t>(decoded));
#else
            if (!warned_opus) {
                PM_WARN("audio.net: Opus datagram received but POLYMATH_USE_OPUS is OFF; "
                        "build with -DPOLYMATH_USE_OPUS=ON to enable. "
                        "Dropping all Opus datagrams (this warning fires once).");
                warned_opus = true;
            }
            continue;
#endif
        } else {
            PM_WARN("audio.net: unknown codec {} in datagram, discarded", hdr.codec);
            continue;
        }

        // Update last-seen room id and fire callback.
        last_room_id_.store(hdr.room_id, std::memory_order_release);
        if (room_cb_) room_cb_(hdr.room_id);
    }
}

} // namespace polymath::audio
