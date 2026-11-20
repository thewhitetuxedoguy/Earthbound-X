#include "xbox_input.hpp"

#include <SDL.h>

namespace eb::xbox {
namespace {

constexpr int kButtonA = 0, kButtonB = 1, kButtonX = 2, kButtonY = 3;
constexpr int kButtonBlack = 4, kButtonWhite = 5, kButtonBack = 6, kButtonStart = 7;
constexpr int kAxisLeftX = 0, kAxisLeftY = 1, kAxisLeftTrigger = 2, kAxisRightTrigger = 5;

constexpr int kStickThreshold = 16000;
// Triggers rest at -32768 and reach +32767 fully pressed.
constexpr int kTriggerThreshold = 0;

} // namespace

Controllers::~Controllers() {
    for (auto *&pad : pads_)
        if (pad) SDL_JoystickClose(pad);
    if (initialized_) SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
}

bool Controllers::init() {
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0) return false;
    initialized_ = true;
    open_all();
    return true;
}

void Controllers::open_all() {
    const int devices = SDL_NumJoysticks();
    for (int i = 0; i < devices && count_ < kMaxPads; ++i) {
        SDL_Joystick *pad = SDL_JoystickOpen(i);
        if (!pad) continue;
        bool already_open = false;
        for (auto *known : pads_) already_open = already_open || known == pad;
        if (already_open) continue;
        for (auto *&slot : pads_) {
            if (!slot) {
                slot = pad;
                ++count_;
                break;
            }
        }
    }
}

std::uint16_t Controllers::read(SDL_Joystick *pad, bool &chord) const {
    std::uint16_t bits = 0;
    const auto button = [&](int index) { return SDL_JoystickGetButton(pad, index) != 0; };

    // Face buttons keep their physical position: Xbox A (bottom) = SNES B (bottom),
    // Xbox X (left) = SNES Y, Xbox B (right) = SNES A, Xbox Y (top) = SNES X.
    if (button(kButtonA)) bits |= kB;
    if (button(kButtonX)) bits |= kY;
    if (button(kButtonB)) bits |= kA;
    if (button(kButtonY)) bits |= kX;
    if (button(kButtonBack)) bits |= kSelect;
    if (button(kButtonStart)) bits |= kStart;
    if (button(kButtonBlack)) bits |= kL;
    if (button(kButtonWhite)) bits |= kR;

    const Uint8 hat = SDL_JoystickGetHat(pad, 0);
    if (hat & SDL_HAT_UP) bits |= kUp;
    if (hat & SDL_HAT_DOWN) bits |= kDown;
    if (hat & SDL_HAT_LEFT) bits |= kLeft;
    if (hat & SDL_HAT_RIGHT) bits |= kRight;

    const int stick_x = SDL_JoystickGetAxis(pad, kAxisLeftX);
    const int stick_y = SDL_JoystickGetAxis(pad, kAxisLeftY); // the driver reports up as negative
    if (stick_x < -kStickThreshold) bits |= kLeft;
    if (stick_x > kStickThreshold) bits |= kRight;
    if (stick_y < -kStickThreshold) bits |= kUp;
    if (stick_y > kStickThreshold) bits |= kDown;

    // Triggers are an alternative L/R for pads whose shoulder buttons are awkward.
    if (SDL_JoystickGetAxis(pad, kAxisLeftTrigger) > kTriggerThreshold) bits |= kL;
    if (SDL_JoystickGetAxis(pad, kAxisRightTrigger) > kTriggerThreshold) bits |= kR;

    chord = chord || (button(kButtonBack) && button(kButtonStart) && button(kButtonBlack) && button(kButtonWhite));

    // Opposing directions cannot happen on a real D-pad; cancel them.
    if ((bits & kLeft) && (bits & kRight)) bits &= static_cast<std::uint16_t>(~(kLeft | kRight));
    if ((bits & kUp) && (bits & kDown)) bits &= static_cast<std::uint16_t>(~(kUp | kDown));
    return bits;
}

std::uint16_t Controllers::poll() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_JOYDEVICEADDED) {
            open_all();
        } else if (event.type == SDL_JOYDEVICEREMOVED) {
            for (auto *&pad : pads_) {
                if (pad && SDL_JoystickInstanceID(pad) == event.jdevice.which) {
                    SDL_JoystickClose(pad);
                    pad = nullptr;
                    --count_;
                }
            }
        }
    }
    SDL_JoystickUpdate();

    std::uint16_t bits = 0;
    bool chord = false;
    for (auto *pad : pads_)
        if (pad) bits |= read(pad, chord);

    exit_hold_ = chord ? exit_hold_ + 1 : 0;
    return bits;
}

} // namespace eb::xbox
