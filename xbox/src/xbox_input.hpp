// Controller input for the original Xbox, via SDL's joystick API.
//
// nxdk's SDL exposes the original controller as a joystick with: buttons
// 0=A 1=B 2=X 3=Y 4=Black 5=White 6=Back 7=Start 8=LThumb 9=RThumb, one hat (D-pad),
// and axes 0/1 = left stick X/Y, 2 = left trigger, 5 = right trigger. The raw
// joystick API is used because SDL's game-controller database has no entry for it.
//
// SNES layout (what GameSession::advance_frame expects), bit 15 down to bit 4:
//   B, Y, Select, Start, Up, Down, Left, Right, A, X, L, R
#pragma once

#include <cstdint>

struct _SDL_Joystick;

namespace eb::xbox {

class Controllers {
public:
    Controllers() = default;
    ~Controllers();
    Controllers(const Controllers &) = delete;
    Controllers &operator=(const Controllers &) = delete;

    // Initializes SDL's joystick subsystem (USB) and opens every pad already present.
    bool init();

    // Pumps SDL events (hotplug) and returns the SNES button bits of all pads OR-ed
    // together. Also tracks the exit chord.
    std::uint16_t poll();

    // True once Back + Start + L + R (Xbox: Back, Start, Black, White) have been held
    // together for about 1.5 seconds of poll() calls.
    bool exit_requested() const { return exit_hold_ >= kExitHoldPolls; }

    int connected() const { return count_; }

    // SNES joypad bit positions.
    static constexpr std::uint16_t kB = 1u << 15, kY = 1u << 14, kSelect = 1u << 13, kStart = 1u << 12;
    static constexpr std::uint16_t kUp = 1u << 11, kDown = 1u << 10, kLeft = 1u << 9, kRight = 1u << 8;
    static constexpr std::uint16_t kA = 1u << 7, kX = 1u << 6, kL = 1u << 5, kR = 1u << 4;

private:
    static constexpr int kMaxPads = 4;
    static constexpr int kExitHoldPolls = 90;

    void open_all();
    std::uint16_t read(_SDL_Joystick *pad, bool &chord) const;

    _SDL_Joystick *pads_[kMaxPads] = {};
    int count_ = 0;
    int exit_hold_ = 0;
    bool initialized_ = false;
};

} // namespace eb::xbox
