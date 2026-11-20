#include "xbox_video.hpp"

#include <hal/video.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

#include <cstring>

namespace eb::xbox {

Screen::Screen(ScreenFit fit) : fit_(fit) {
    XVideoSetMode(kWidth, kHeight, 32, REFRESH_DEFAULT);
    refresh_hz_ = XVideoGetMode().refresh > 0 ? XVideoGetMode().refresh : 60;
    front_ = XVideoGetFB();

    // Second framebuffer, allocated like the HAL does for the first one. If this fails
    // we keep drawing into the single scanned-out buffer (it can tear, but works).
    constexpr std::size_t bytes = kWidth * kHeight * 4;
    back_ = static_cast<std::uint8_t *>(MmAllocateContiguousMemoryEx(
        bytes, 0x00000000, 0x7FFFFFFF, 0x1000, PAGE_READWRITE | PAGE_WRITECOMBINE));
    if (back_) clear(back_);

    if (fit_ == ScreenFit::Integer2x) {
        dest_w_ = kSourceWidth * 2;
        dest_h_ = kSourceHeight * 2;
        dest_x_ = (kWidth - dest_w_) / 2;
        dest_y_ = (kHeight - dest_h_) / 2;
    }
    source_x_.resize(static_cast<std::size_t>(dest_w_));
    source_y_.resize(static_cast<std::size_t>(dest_h_));
    for (int x = 0; x < dest_w_; ++x) source_x_[x] = static_cast<std::uint16_t>(x * kSourceWidth / dest_w_);
    for (int y = 0; y < dest_h_; ++y) source_y_[y] = static_cast<std::uint16_t>(y * kSourceHeight / dest_h_);
    line_.assign(static_cast<std::size_t>(kWidth), 0xFF000000u);

    // Borders (integer mode) are cleared once in both buffers; present() never touches them.
    clear(front_);
}

void Screen::clear(std::uint8_t *buffer) {
    std::memset(buffer, 0, static_cast<std::size_t>(kWidth) * kHeight * 4);
}

void Screen::wait_vblank() { XVideoWaitForVBlank(); }

void Screen::present(std::span<const std::uint32_t, kSourceWidth * kSourceHeight> pixels) {
    std::uint8_t *target = back_ ? back_ : front_;
    int cached_source_row = -1;

    for (int y = 0; y < dest_h_; ++y) {
        const int source_row = source_y_[static_cast<std::size_t>(y)];
        if (source_row != cached_source_row) {
            const std::uint32_t *row = pixels.data() + source_row * kSourceWidth;
            std::uint32_t *line = line_.data() + dest_x_;
            for (int x = 0; x < dest_w_; ++x) line[x] = row[source_x_[static_cast<std::size_t>(x)]] | 0xFF000000u;
            cached_source_row = source_row;
        }
        std::memcpy(target + (static_cast<std::size_t>(dest_y_ + y) * kWidth + dest_x_) * 4,
                    line_.data() + dest_x_, static_cast<std::size_t>(dest_w_) * 4);
    }
    XVideoFlushFB(); // sfence: make the write-combined stores visible to the display

    if (back_) {
        // Flip during vblank so the scanout never shows a half-drawn frame.
        XVideoWaitForVBlank();
        XVideoSetFB(back_);
        std::uint8_t *shown = back_;
        back_ = front_;
        front_ = shown;
    }
}

} // namespace eb::xbox
