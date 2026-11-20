// Win32 calls the Phase Distorter sources make under `#ifdef _WIN32` that nxdk's
// WinAPI subset lacks. The sources pass `path.c_str()` (a narrow string on
// Xbox, see eb_xbox_filesystem.hpp) to the "W" functions, so these shims take
// `const char *`. Upstream files are not modified.
#pragma once

#include <cstring>
#include <ctime>

#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

// The Win16-era `near`/`far` macros from windef.h would mangle ordinary identifiers.
#undef near
#undef far

#ifndef MOVEFILE_REPLACE_EXISTING
#define MOVEFILE_REPLACE_EXISTING 0x00000001
#endif
#ifndef MOVEFILE_WRITE_THROUGH
#define MOVEFILE_WRITE_THROUGH 0x00000008
#endif
// FATX has no reparse points/symlinks, so these checks are vacuous on Xbox.
#ifndef FILE_ATTRIBUTE_REPARSE_POINT
#define FILE_ATTRIBUTE_REPARSE_POINT 0x00000400
#endif
#ifndef FILE_FLAG_OPEN_REPARSE_POINT
#define FILE_FLAG_OPEN_REPARSE_POINT 0
#endif

// nxdk's MoveFileA uses ReplaceIfExists = FALSE, so it cannot replace a file.
// With MOVEFILE_REPLACE_EXISTING, park the old file, move the new one in, drop
// the parked copy, and restore the old file if the final move fails. A crash
// between the two moves leaves "<dest>.eb_old" holding the previous good file.
inline BOOL MoveFileExW(const char *from, const char *to, DWORD flags) {
    const DWORD target = GetFileAttributesA(to);
    const bool replace = (flags & MOVEFILE_REPLACE_EXISTING) != 0 && target != INVALID_FILE_ATTRIBUTES &&
                         (target & FILE_ATTRIBUTE_DIRECTORY) == 0;
    if (!replace) return MoveFileA(from, to);

    char parked[512];
    if (std::strlen(to) + 8 >= sizeof(parked)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    std::strcpy(parked, to);
    std::strcat(parked, ".eb_old");
    DeleteFileA(parked);
    if (!MoveFileA(to, parked)) return FALSE;
    if (!MoveFileA(from, to)) {
        const DWORD error = GetLastError();
        MoveFileA(parked, to);
        SetLastError(error);
        return FALSE;
    }
    DeleteFileA(parked);
    return TRUE;
}

inline BOOL DeleteFileW(const char *path) { return DeleteFileA(path); }

inline HANDLE CreateFileW(const char *path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
                          DWORD disposition, DWORD flags, HANDLE templ) {
    return CreateFileA(path, access, share, security, disposition, flags, templ);
}

inline BOOL FlushFileBuffers(HANDLE file) {
    IO_STATUS_BLOCK status_block;
    const NTSTATUS status = NtFlushBuffersFile(file, &status_block);
    if (!NT_SUCCESS(status)) {
        SetLastError(RtlNtStatusToDosError(status));
        return FALSE;
    }
    return TRUE;
}

inline int localtime_s(std::tm *out, const std::time_t *value) {
    const std::tm *local = std::localtime(value);
    if (!local) return 1;
    *out = *local;
    return 0;
}
