#include "xbox_game.hpp"

#include <hal/debug.h>
#include <hal/xbox.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

#include <memory>
#include <string>
#include <vector>

#include <eb_xbox_filesystem.hpp>

#include "eb/asset_store.hpp"
#include "eb/game_session.hpp"
#include "eb/session_storage.hpp"
#include "generated_assets.hpp"
#include "xbox_audio.hpp"
#include "xbox_input.hpp"
#include "xbox_platform.hpp"
#include "xbox_video.hpp"

namespace eb::xbox {
namespace {

namespace fs = eb::xbox::fs;

// SNES NTSC frame rate. The simulation is driven by this clock, not by the display's
// refresh rate, so game speed and audio pitch stay correct on 50 Hz and 60 Hz consoles.
constexpr double kNativeFrameRate = 60.09881389744051;
constexpr int kMaxCatchUpFrames = 3;
constexpr int kSaveCheckInterval = 30; // simulation frames between SRAM comparisons
constexpr int kSaveStableChecks = 2;   // unchanged this many checks (~1 s) before writing
constexpr int kStatsInterval = 600;    // simulation frames between performance log lines

[[noreturn]] void halt() {
    for (;;) Sleep(1000);
}

std::string find_pack(const std::string &file_name) {
    const std::string candidates[] = {"E:\\EarthBound\\" + file_name, "D:\\" + file_name};
    for (const auto &candidate : candidates) {
        std::error_code ec;
        if (fs::exists(fs::path(candidate), ec)) return candidate;
    }
    return {};
}

[[noreturn]] void missing_pack() {
    const std::string us = std::string(eb::game_basename(eb::GameVersion::US)) + ".ebpak";
    const std::string jp = std::string(eb::game_basename(eb::GameVersion::JP)) + ".ebpak";
    eb::xbox::cout << "EarthBound - asset pack not found\n\n"
                   << "Expected one of (E: is the HDD):\n"
                   << "  E:\\EarthBound\\" << us << "   (US)\n"
                   << "  E:\\EarthBound\\" << jp << "   (JP)\n"
                   << "  or the same file name in the disc root (D:\\)\n\n"
                   << "Create it on a PC from your own ROM, with the desktop build:\n"
                   << "  eb_cpp --import-rom <your ROM file> --import-only\n"
                   << "then copy the resulting .ebpak to the console.\n";
    halt();
}

// Writes battery RAM once it has changed and then stayed unchanged for a moment, so a
// save in progress is never captured half-written and the disk is not hammered.
class SaveWatcher {
public:
    SaveWatcher(std::string path, std::span<const std::uint8_t> initial)
        : path_(std::move(path)), written_(initial.begin(), initial.end()) {}

    void check(std::span<const std::uint8_t> sram) {
        if (equals(sram, written_)) {
            pending_.clear();
            stable_ = 0;
            return;
        }
        if (!pending_.empty() && equals(sram, pending_)) {
            if (++stable_ >= kSaveStableChecks) write(sram);
            return;
        }
        pending_.assign(sram.begin(), sram.end());
        stable_ = 0;
    }

    void flush(std::span<const std::uint8_t> sram) {
        if (!equals(sram, written_)) write(sram);
    }

private:
    static bool equals(std::span<const std::uint8_t> a, const std::vector<std::uint8_t> &b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
    }
    void write(std::span<const std::uint8_t> sram) {
        eb::store_save(path_, sram);
        written_.assign(sram.begin(), sram.end());
        pending_.clear();
        stable_ = 0;
        eb::xbox::cout << "saved " << path_ << '\n';
    }

    std::string path_;
    std::vector<std::uint8_t> written_;
    std::vector<std::uint8_t> pending_;
    int stable_ = 0;
};

} // namespace

[[noreturn]] void run_game() {
    // The region played is whichever pack is present (US first). Each region has its own
    // pack and save file, so the two never mix.
    eb::GameVersion version = eb::GameVersion::US;
    std::string pack_path = find_pack(std::string(eb::game_basename(version)) + ".ebpak");
    if (pack_path.empty()) {
        version = eb::GameVersion::JP;
        pack_path = find_pack(std::string(eb::game_basename(version)) + ".ebpak");
    }
    if (pack_path.empty()) missing_pack();
    const std::string base = eb::game_basename(version);
    const std::string save_path = "E:\\EarthBound\\" + base + ".srm";
    eb::xbox::cout << "loading " << pack_path << '\n';

    // Validates the pack's fingerprint against the built-in profiles and rebuilds the
    // complete address image. A bad or mismatched pack ends in the fatal-error screen
    // with the reason.
    eb::GameAssets program = eb::load_game_assets(eb::native_path(pack_path), eb::asset_profiles());
    if (program.version != version)
        fatal("The asset pack is for a different region than its file name says.", __FILE__, __LINE__);
    eb::xbox::cout << "loaded " << program.title << " (" << program.image.size() << " bytes)\n";

    // Original timing: the translated game program drives the hardware exactly as the
    // cartridge would, with no host-side gameplay or artwork replacements.
    auto session = std::make_unique<eb::GameSession>(program.image, program.version, false);
    eb::load_save(save_path, session->save_memory());
    SaveWatcher saves(save_path, session->save_memory());

    Controllers pads;
    pads.init();
    AudioOutput &audio = audio_output();
    const bool audio_ok = audio.start();
    eb::xbox::cout << "audio " << (audio_ok ? "started" : "unavailable") << ", pads " << pads.connected() << '\n';

    // From here the game owns the screen; the text console would draw over it.
    set_screen_console_enabled(false);
    Screen screen(ScreenFit::Stretch4x3);

    const double ticks_per_second = static_cast<double>(KeQueryPerformanceFrequency());
    const double frame_ticks = ticks_per_second / kNativeFrameRate;
    const auto to_ms = [&](ULONGLONG ticks) { return static_cast<double>(ticks) * 1000.0 / ticks_per_second; };

    double owed = 0;
    ULONGLONG last = KeQueryPerformanceCounter();
    int frames_since_save_check = 0, frames_in_window = 0;
    double window_ms = 0, worst_ms = 0;

    while (!pads.exit_requested()) {
        const ULONGLONG now = KeQueryPerformanceCounter();
        owed += static_cast<double>(now - last);
        last = now;

        const std::uint16_t buttons = pads.poll();

        int stepped = 0;
        while (owed >= frame_ticks && stepped < kMaxCatchUpFrames) {
            const ULONGLONG begin = KeQueryPerformanceCounter();
            session->advance_frame(buttons);
            const auto samples = session->take_audio_samples();
            if (audio_ok) audio.append(samples);
            const double ms = to_ms(KeQueryPerformanceCounter() - begin);
            window_ms += ms;
            if (ms > worst_ms) worst_ms = ms;
            owed -= frame_ticks;
            ++stepped;

            if (++frames_since_save_check >= kSaveCheckInterval) {
                frames_since_save_check = 0;
                saves.check(session->save_memory());
            }
            if (++frames_in_window >= kStatsInterval) {
                eb::xbox::cout << "sim avg " << window_ms / frames_in_window << " ms, worst " << worst_ms
                               << " ms (budget " << 1000.0 / kNativeFrameRate << " ms), audio underruns "
                               << audio.underruns() << ", dropped " << audio.dropped_batches() << ", queued "
                               << audio.queued_frames() << '\n';
                frames_in_window = 0;
                window_ms = worst_ms = 0;
            }
        }
        // Far behind (e.g. a long save write): resynchronize instead of fast-forwarding.
        if (owed > frame_ticks * (kMaxCatchUpFrames + 1)) owed = 0;

        if (stepped > 0)
            screen.present(session->native_pixels()); // flips at vblank, which also paces the loop
        else
            screen.wait_vblank();
    }

    saves.flush(session->save_memory());
    eb::xbox::cout << "exit requested, rebooting\n";
    XReboot();
    halt();
}

} // namespace eb::xbox
