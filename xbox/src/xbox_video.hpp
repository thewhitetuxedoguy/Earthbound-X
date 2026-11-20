// Video output for the original Xbox.
//
// Writes straight into the HAL framebuffer instead of going through SDL's window
// surface (which converts every pixel on every update). Game pixels are 0xAARRGGBB,
// which is exactly the 32-bit X8R8G8B8 layout of the framebuffer, so no channel
// conversion is needed. Two framebuffers are flipped with XVideoSetFB at vblank.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace eb::xbox {

enum class ScreenFit {
    Stretch4x3, // fill 640x480; the SNES picture is meant for a 4:3 display
    Integer2x,  // exact 2x (512x448) centered with black borders
};

class Screen {
public:
    static constexpr int kSourceWidth = 256;
    static constexpr int kSourceHeight = 224;
    static constexpr int kWidth = 640;
    static constexpr int kHeight = 480;

    explicit Screen(ScreenFit fit = ScreenFit::Stretch4x3);

    // Scales `pixels` into the back buffer and flips it at the next vblank.
    void present(std::span<const std::uint32_t, kSourceWidth * kSourceHeight> pixels);

    // Blocks until the start of the next vertical blank.
    void wait_vblank();

    // Refresh rate of the active video mode in Hz (60 on NTSC, 50 on PAL).
    int refresh_hz() const { return refresh_hz_; }
    bool double_buffered() const { return back_ != nullptr; }

private:
    void clear(std::uint8_t *buffer);

    ScreenFit fit_;
    int refresh_hz_ = 60;
    std::uint8_t *front_ = nullptr; // currently scanned out
    std::uint8_t *back_ = nullptr;  // being drawn (null if allocation failed)
    int dest_x_ = 0, dest_y_ = 0, dest_w_ = kWidth, dest_h_ = kHeight;
    std::vector<std::uint16_t> source_x_; // per destination column
    std::vector<std::uint16_t> source_y_; // per destination row
    std::vector<std::uint32_t> line_;     // cached row, avoids reading write-combined memory
};

} // namespace eb::xbox
