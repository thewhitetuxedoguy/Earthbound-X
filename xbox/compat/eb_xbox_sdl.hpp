// SDL wrapper for nxdk. nxdk bundles SDL 2.0.9, which predates a few
// SDL_GameControllerButton values that the controller code switches over
// (added in 2.0.14). Distinct placeholder values (negative: they must stay inside the enum
// range [-16, 15] and clear of INVALID == -1) keep those `case` labels valid;
// the Xbox controller never reports them. prepare_sources.py rewrites
// `#include <SDL.h>` to include this header.
#pragma once

#include <SDL.h>

// Added in SDL 2.0.12. Unknown hints are ignored by older SDL, so the name alone is enough.
#ifndef SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS
#define SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS "SDL_GAMECONTROLLER_USE_BUTTON_LABELS"
#endif

#if !SDL_VERSION_ATLEAST(2, 0, 14)
#define SDL_CONTROLLER_BUTTON_MISC1 ((SDL_GameControllerButton)(-2))
#define SDL_CONTROLLER_BUTTON_PADDLE1 ((SDL_GameControllerButton)(-3))
#define SDL_CONTROLLER_BUTTON_PADDLE2 ((SDL_GameControllerButton)(-4))
#define SDL_CONTROLLER_BUTTON_PADDLE3 ((SDL_GameControllerButton)(-5))
#define SDL_CONTROLLER_BUTTON_PADDLE4 ((SDL_GameControllerButton)(-6))
#define SDL_CONTROLLER_BUTTON_TOUCHPAD ((SDL_GameControllerButton)(-7))
#endif
