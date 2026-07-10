#pragma once
//
// Shared audio constants and lock-free / single-thread ring buffers used by
// the capture callback and the AudioService pump thread.
//
// Pipeline format (openWakeWord, Silero VAD, whisper.cpp, Piper):
//   * 16 000 Hz sample rate
//   * 1 channel (mono)
//   * float32 samples in [-1, 1]
//
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace polymath::audio {

// Canonical pipeline format.
inline constexpr int   kSampleRate   = 16000;
inline constexpr int   kChannels     = 1;
inline constexpr float kSampleScale  = 32768.0f;   // int16 <-> float

// openWakeWord hop / Silero window.
inline constexpr int   kFrameSamples = 1280;       // 80 ms @ 16 kHz (oWW hop)
inline constexpr int   kVadWindow    = 512;        // 32 ms @ 16 kHz (Silero)

// Capture ring: 16 s of headroom (was 4 s) so ASR/TTS stalls never overflow.
// 16 * 16000 = 256000; next power-of-two = 262144 = 1 << 18.
inline constexpr size_t kRingCapacity = 1u << 18;

// oWW hangover after VAD speech ends (keep feeding the classifier so mid-phrase
// context is not lost). Spec 04 §3.1: 640 ms.
inline constexpr int kOwwHangoverMs = 640;

// Pre-roll kept before a VAD speech-start so wake-phrase / command onset is not
// dropped when Silero fires slightly late.
inline constexpr int kPreRollMs = 1000;

// A lock-free SPSC ring of float samples.  Only one producer (the audio
// callback) and one consumer (the worker) may touch it concurrently.  Capacity
// is rounded up to a power of two so we can mask instead of modulo.
class FloatRing {
public:
    explicit FloatRing(size_t capacity_pow2 = kRingCapacity) {
        size_t cap = 1;
        while (cap < capacity_pow2) cap <<= 1;
        buf_.resize(cap);
        mask_ = cap - 1;
    }

    // Producer side: push up to n samples, returns how many were written.
    // Samples that do not fit are counted in drops() and discarded.
    size_t write(const float* src, size_t n) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);
        const size_t free_space = capacity() - (head - tail);
        const size_t to_write = n < free_space ? n : free_space;
        for (size_t i = 0; i < to_write; ++i)
            buf_[(head + i) & mask_] = src[i];
        head_.store(head + to_write, std::memory_order_release);
        if (to_write < n)
            drops_.fetch_add(n - to_write, std::memory_order_relaxed);
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

    // Cumulative samples dropped because the ring was full (producer overflow).
    size_t drops() const { return drops_.load(std::memory_order_relaxed); }
    void   resetDrops()  { drops_.store(0, std::memory_order_relaxed); }

    void clear() {
        tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release);
    }

private:
    std::vector<float>  buf_;
    size_t              mask_ = 0;
    std::atomic<size_t> head_{0};   // producer
    std::atomic<size_t> tail_{0};   // consumer
    std::atomic<size_t> drops_{0};
};

// Single-thread fixed-capacity circular sample buffer (no heap churn on the
// hot path). Replaces deque push/pop_front for pre-roll and similar uses.
class SampleRing {
public:
    explicit SampleRing(size_t capacity = 0) { reset(capacity); }

    void reset(size_t capacity) {
        cap_ = capacity;
        buf_.assign(capacity, 0.0f);
        head_ = 0;
        size_ = 0;
    }

    size_t capacity() const { return cap_; }
    size_t size()     const { return size_; }
    bool   empty()    const { return size_ == 0; }

    void push(const float* src, size_t n) {
        if (cap_ == 0 || src == nullptr || n == 0) return;
        for (size_t i = 0; i < n; ++i) {
            buf_[head_] = src[i];
            head_ = (head_ + 1) % cap_;
            if (size_ < cap_) ++size_;
        }
    }

    // Append chronological contents (oldest → newest) into `out`.
    void appendTo(std::vector<float>& out) const {
        if (size_ == 0) return;
        const size_t start = (head_ + cap_ - size_) % cap_;
        out.reserve(out.size() + size_);
        for (size_t i = 0; i < size_; ++i)
            out.push_back(buf_[(start + i) % cap_]);
    }

    // Copy chronological contents into a fresh vector.
    std::vector<float> snapshot() const {
        std::vector<float> out;
        appendTo(out);
        return out;
    }

    void clear() { head_ = 0; size_ = 0; }

private:
    std::vector<float> buf_;
    size_t cap_  = 0;
    size_t head_ = 0;
    size_t size_ = 0;
};

// RMS energy of a float PCM block (for barge-in energy gate).
inline float frameRms(const float* data, int n) {
    if (!data || n <= 0) return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double v = static_cast<double>(data[i]);
        sum += v * v;
    }
    return static_cast<float>(std::sqrt(sum / n));
}

} // namespace polymath::audio
