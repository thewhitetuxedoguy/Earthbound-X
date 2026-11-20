// The EarthBound game loop for the original Xbox.
#pragma once

namespace eb::xbox {

// Loads the user's asset pack, runs the game until the exit chord is held, saves,
// and reboots to the dashboard. If the pack is missing it shows instructions and
// waits (it does not return).
[[noreturn]] void run_game();

} // namespace eb::xbox
