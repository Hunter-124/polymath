#pragma once
//
// Shared audio constants and a tiny lock-free single-producer/single-consumer
// ring buffer used to hand 16 kHz mono float PCM from the miniaudio capture
// callback (realtime thread) to the AudioService worker thread.
//
// The whole pipeline standardises on:
//   * 16 000 Hz sample rate
//   * 1 channel (mono)
//   * float32 samples in [-1, 1]
// which is what openWakeWord, Silero VAD, whisper.cpp and Piper all expect.
//
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace polymath::audio {

// Canonical pipeline format.
inline constexpr int   kSampleRate   = 16000;
inline constexpr int   kChannels     = 1;
inline constexpr float kSampleScale  = 32768.0f;   // int16 <-> float

// openWakeWord / Silero operate on fixed frame sizes.
inline constexpr int   kFrameSamples = 1280;       // 80 ms @ 16 kHz (oWW hop)

// A lock-free SPSC ring of float samples.  Only one producer (the audio
// callback) and one consumer (the worker) may touch it concurrently.  Capacity
// is rounded up to a power of two so we can mask instead of modulo.
class FloatRing {
public:
    explicit FloatRing(size_t capacity_pow2 = 1u << 16) {
        size_t cap = 1;
        while (cap < capacity_pow2) cap <<= 1;
        buf_.resize(cap);
        mask_ = cap - 1;
    }

    // Producer side: push up to n samples, returns how many were written.
    size_t write(const float* src, size_t n) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);
        const size_t free_space = capacity() - (head - tail);
        const size_t to_write = n < free_space ? n : free_space;
        for (size_t i = 0; i < to_write; ++i)
            buf_[(head + i) & mask_] = src[i];
        head_.store(head + to_write, std::memory_order_release);
        return to_write;
    }

    // Consumer side: read up to n samples, returns how many were read.
    size_t read(float* dst, size_t n) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t avail = head - tail;
        const size_t to_read = n < avail ? n : avail;
        for (size_t i = 0; i < to_read; ++i)
            dst[i] = buf_[(tail + i) & mask_];
        tail_.store(tail + to_read, std::memory_order_release);
        return to_read;
    }

    size_t available() const {
        return head_.load(std::memory_order_acquire) -
               tail_.load(std::memory_order_acquire);
    }

    size_t capacity() const { return mask_ + 1; }

    void clear() {
        tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release);
    }

private:
    std::vector<float>  buf_;
    size_t              mask_ = 0;
    std::atomic<size_t> head_{0};   // producer
    std::atomic<size_t> tail_{0};   // consumer
};

} // namespace polymath::audio
