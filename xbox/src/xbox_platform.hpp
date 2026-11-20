// Xbox platform layer shared declarations.
#pragma once

#include <cstddef>

namespace eb::xbox {

// Console output. Always goes to the kernel debug log (visible in xemu / debug
// kits); additionally to the on-screen nxdk debug console once enabled.
void log_write(const char *text, std::size_t length);
void set_screen_console_enabled(bool enabled);

// Best-effort path of the fatal log file (E: is the writable HDD partition).
inline constexpr const char *kFatalLogPath = "E:\\EarthBound\\fatal.log";

} // namespace eb::xbox
