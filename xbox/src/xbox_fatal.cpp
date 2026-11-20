// eb::xbox::fatal: every `throw` in the prepared sources ends up here.
#include <hal/debug.h>
#include <hal/video.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

#include <cstdio>
#include <cstring>

#include "../compat/eb_xbox_compat.hpp"
#include "xbox_platform.hpp"

namespace eb::xbox {

[[noreturn]] void fatal(const char *what, const char *file, int line) {
    char message[768];
    std::snprintf(message, sizeof(message), "EarthBound fatal error\n%s\n%s:%d\n", what ? what : "(null)",
                  file ? file : "?", line);

    DbgPrint("%s", message);

    // Best effort: keep a copy on the HDD so the error survives a power cycle.
    CreateDirectoryA("E:\\EarthBound", nullptr);
    HANDLE log = CreateFileA(kFatalLogPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(log, message, static_cast<DWORD>(std::strlen(message)), &written, nullptr);
        CloseHandle(log);
    }

    // Show it on the TV. If a game video mode is not active yet, set a plain one.
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    debugPrint("%s\nPower off the console to exit.\n", message);

    for (;;) Sleep(1000);
}

} // namespace eb::xbox
