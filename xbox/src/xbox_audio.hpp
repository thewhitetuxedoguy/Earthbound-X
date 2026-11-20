// Audio output for the original Xbox (AC97 via nxdk's XAudio).
//
// The game produces interleaved stereo int16 at the SNES DSP rate of 32 kHz. The
// hardware is fixed at 48 kHz, so samples are resampled 3:2 with linear
// interpolation. The XAudio callback runs in a DPC: it uses integer math only, never
// allocates and never blocks. The game loop is the only producer, the callback the
// only consumer, so a lock-free single-producer/single-consumer ring is enough.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace eb::xbox {

class AudioOutput {
public:
    static constexpr std::uint32_t kSourceRate = 32000;
    static constexpr std::uint32_t kOutputRate = 48000;

    // Allocates DMA buffers, starts XAudio and begins pulling from the ring.
    // Returns false if the buffers could not be allocated.
    bool start();

    // Queue native 32 kHz interleaved stereo samples (left, right, left, ...).
    // If the ring is already holding more than ~170 ms the batch is dropped, which
    // bounds latency if the simulation runs ahead of the hardware clock.
    void append(std::span<const std::int16_t> stereo_samples);

    std::uint32_t underruns() const { return underruns_.load(std::memory_order_relaxed); }
    std::uint32_t dropped_batches() const { return dropped_.load(std::memory_order_relaxed); }
    std::uint32_t queued_frames() const;

    // Used by the XAudio callback; public only so the C callback can reach it.
    void fill_next_buffer();

private:
    static constexpr std::uint32_t kRingFrames = 16384; // power of two, 512 ms at 32 kHz
    static constexpr std::uint32_t kMaxQueuedFrames = 5600;
    static constexpr std::uint32_t kPrimeFrames = 1600; // ~50 ms before playback starts
    static constexpr int kBufferCount = 3;
    static constexpr unsigned short kBufferBytes = 4096; // 1024 stereo frames, ~21 ms

    void fill(std::int16_t *out, std::uint32_t frames);

    std::int16_t ring_[kRingFrames * 2] = {};
    std::atomic<std::uint32_t> write_{0}; // frames written, monotonic (producer)
    std::atomic<std::uint32_t> read_{0};  // frames consumed, monotonic (consumer)
    std::atomic<std::uint32_t> underruns_{0};
    std::atomic<std::uint32_t> dropped_{0};
    unsigned char *buffers_[kBufferCount] = {};
    int next_buffer_ = 0;
    std::uint32_t phase_ = 0; // position between source frames, in thirds
    bool priming_ = true;
};

// The single audio device instance (XAudio has one output, and the callback needs a
// stable address).
AudioOutput &audio_output();

} // namespace eb::xbox
