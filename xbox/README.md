# Phase Distorter — Xbox (nxdk) port

Personal, non-commercial experiment. This directory adds an nxdk build next to the
upstream sources. **Upstream files are never modified**: `tools/prepare_sources.py`
copies `cpp/src` and `cpp/include` into `build/port/`, applies mechanical rewrites, and
mirrors `generated/` and the SPC DSP into `build/` with symlinks.

## Build

```sh
# nxdk with ALL submodules populated (the GitHub zip ships them empty), clang + lld + llvm
eval "$(/path/to/nxdk/bin/activate -s)"
make -C xbox NXDK_DIR=/path/to/nxdk        # -> bin/default.xbe, EarthBound.iso
```

A full build compiles ~1,900 objects (both regions' translated programs); budget ~25 min
on one core. `make` is incremental, so it can be interrupted and resumed.

Environment notes (all learned the hard way):

* Submodule versions nxdk expects: libjpeg-turbo **2.0.4**, SDL_ttf **release-2.0.14**
  (needs `SDL_ttf.c` at the root and bundled FreeType 2.4.12), SDL_image **release-2.0.5**.
  Newer branch heads are SDL3-era and use a different layout.
* Submodules were cloned from branch heads because the GitHub zip has no pinned commits.
* Port flags are scoped to the port's own objects (target-specific `CXXFLAGS`); applying them
  globally breaks nxdk's own libc++ build.

## Using it

1. On a PC, create the asset pack from **your own ROM** with the desktop build (check
   `eb_cpp --help` for the exact import flags; the on-screen hint assumes
   `--import-rom <ROM> --import-only`).
2. Copy `earthbound.ebpak` (US) or `mother2.ebpak` (JP) to `E:\EarthBound\` on the console (or
   emulated HDD). A pack in the disc root (`D:\`) also works. The first one found is used,
   US before JP. Saves go to `E:\EarthBound\<name>.srm`.
3. Run `default.xbe` / the ISO. If the pack is missing, instructions are shown on screen.
4. Optional: create an empty file `E:\EarthBound\selftest` to run the storage self-test at boot.

Controls (original Xbox controller): A/X/B/Y = SNES B/Y/A/X, Black/White = L/R (triggers
also work), Back = Select, Start = Start, D-pad or left stick = directions.
**Hold Back + Start + Black + White for ~1.5 s** to save and reboot to the dashboard.

## What the compat layer does (`compat/`)

| Problem on nxdk | Solution |
| --- | --- |
| `-fno-exceptions` (1,995 `throw`, 221 `catch` upstream) | `eb_xbox_compat.hpp`: `throw` → fatal error screen + `E:\EarthBound\fatal.log`; `try/catch` bodies become dead code |
| libc++ 10 lacks `std::bit_cast` and every `operator<=>` | shims in the same header |
| snes_spc `throw()` specs | `BLARGG_DISABLE_NOTHROW` |
| No `std::filesystem` implementation | `eb_xbox_filesystem.hpp` (`eb::xbox::fs`) over nxdk WinAPI |
| `MoveFileA` cannot replace a file | `MoveFileExW` shim: park old file as `*.eb_old`, move new, drop old, restore on failure |
| `std::cout` missing (`_LIBCPP_HAS_NO_STDOUT`) | `eb::xbox::cout/cerr` → `DbgPrint` + on-screen console |
| SDL 2.0.9 lacks newer controller buttons/hint | `eb_xbox_sdl.hpp` |
| `near`/`far` macros from windef.h | `#undef` in `eb_xbox_win32.hpp` |

## Platform layer (`src/`)

`xbox_video` (double-buffered, vblank flip, nearest 4:3 scale), `xbox_audio` (XAudio, 32→48 kHz
integer resampler, lock-free ring), `xbox_input` (raw SDL joystick), `xbox_game` (pack loading,
fixed-rate simulation clock, save watcher, exit chord), `xbox_log`/`xbox_fatal`,
`xbox_frame_image` (upstream's `write_ppm` without its OpenGL file), `xbox_main`.

## Status (what is verified)

* **Builds and links** into one `default.xbe` (13.1 MB; image sections: 6.8 MB code,
  5.6 MB read-only data, 0.1 MB data). 0 compile errors, 0 undefined symbols.
* The boot **storage self-test passed in an emulator** (filesystem shim, safe file replace,
  directory iteration, `E:` mount).
* **Never run with a real asset pack.** Unverified: that the game boots and renders, frame
  rate on a 733 MHz Pentium III, memory use under 64 MB, audio sync (the loop logs average
  and worst simulation time, underruns and drops every 600 frames to the debug log),
  `XVideoSetFB` double-buffering on hardware, controller mapping, the exit chord.
* Runs in **original-timing mode** only (no host-side gameplay/artwork enhancements).
* **Not ported:** CRT filter, ImGui debug panel, widescreen/presentation pipeline, the
  desktop `desktop_application` loop (replaced by `xbox_game`), the Windows entry point.
