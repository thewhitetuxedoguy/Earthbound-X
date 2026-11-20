#include "xbox_audio.hpp"

#include <hal/audio.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

#include <algorithm>
#include <cstring>

namespace eb::xbox {
namespace {

constexpr ULONG kMaxRam = 0x03FFAFFF;

// Called by XAudio from a DPC whenever a hardware buffer finishes.
void xaudio_callback(void *, void *) { audio_output().fill_next_buffer(); }

} // namespace

AudioOutput &audio_output() {
    static AudioOutput instance;
    return instance;
}

bool AudioOutput::start() {
    for (auto &buffer : buffers_) {
        buffer = static_cast<unsigned char *>(
            MmAllocateContiguousMemoryEx(kBufferBytes, 0, kMaxRam, 0, PAGE_READWRITE | PAGE_WRITECOMBINE));
        if (!buffer) return false;
        std::memset(buffer, 0, kBufferBytes);
    }
    XAudioInit(16, 2, xaudio_callback, nullptr);
    // Hand every buffer to the hardware (silence until the ring has primed), then play.
    for (int i = 0; i < kBufferCount; ++i) fill_next_buffer();
    XAudioPlay();
    return true;
}

std::uint32_t AudioOutput::queued_frames() const {
    return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire);
}

void AudioOutput::append(std::span<const std::int16_t> stereo_samples) {
    const auto frames = static_cast<std::uint32_t>(stereo_samples.size() / 2);
    if (frames == 0) return;
    const std::uint32_t write = write_.load(std::memory_order_relaxed);
    const std::uint32_t queued = write - read_.load(std::memory_order_acquire);
    if (queued + frames > kMaxQueuedFrames) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    for (std::uint32_t i = 0; i < frames; ++i) {
        const std::uint32_t slot = (write + i) & (kRingFrames - 1);
        ring_[slot * 2] = stereo_samples[i * 2];
        ring_[slot * 2 + 1] = stereo_samples[i * 2 + 1];
    }
    write_.store(write + frames, std::memory_order_release);
}

void AudioOutput::fill_next_buffer() {
    auto *buffer = buffers_[next_buffer_];
    fill(reinterpret_cast<std::int16_t *>(buffer), kBufferBytes / 4);
    XAudioProvideSamples(buffer, kBufferBytes, FALSE);
    next_buffer_ = (next_buffer_ + 1) % kBufferCount;
}

// Produces `frames` stereo frames at 48 kHz from the 32 kHz ring. Each output frame
// advances the source position by 2/3 of a frame; `phase_` counts thirds. Integer math
// only: the XAudio callback must not touch the FPU.
void AudioOutput::fill(std::int16_t *out, std::uint32_t frames) {
    std::uint32_t read = read_.load(std::memory_order_relaxed);
    const std::uint32_t write = write_.load(std::memory_order_acquire);
    std::uint32_t available = write - read;

    if (priming_) {
        if (available < kPrimeFrames) {
            std::memset(out, 0, frames * 4);
            return;
        }
        priming_ = false;
        phase_ = 0;
    }

    std::uint32_t produced = 0;
    for (; produced < frames; ++produced) {
        if (available < 2) { // need the next source frame to interpolate
            underruns_.fetch_add(1, std::memory_order_relaxed);
            priming_ = true;
            break;
        }
        const std::uint32_t a = read & (kRingFrames - 1);
        const std::uint32_t b = (read + 1) & (kRingFrames - 1);
        const std::int32_t weight_b = static_cast<std::int32_t>(phase_);
        const std::int32_t weight_a = 3 - weight_b;
        out[produced * 2] = static_cast<std::int16_t>((ring_[a * 2] * weight_a + ring_[b * 2] * weight_b) / 3);
        out[produced * 2 + 1] =
            static_cast<std::int16_t>((ring_[a * 2 + 1] * weight_a + ring_[b * 2 + 1] * weight_b) / 3);
        phase_ += 2;
        if (phase_ >= 3) {
            phase_ -= 3;
            ++read;
            --available;
        }
    }
    if (produced < frames) std::memset(out + produced * 2, 0, (frames - produced) * 4);
    read_.store(read, std::memory_order_release);
}

} // namespace eb::xbox
