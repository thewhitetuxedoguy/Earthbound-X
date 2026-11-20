// Xbox entry point: mounts the HDD, optionally runs the storage self-test, then runs
// the game (xbox_game.cpp).
#include <hal/debug.h>
#include <hal/video.h>
#include <nxdk/mount.h>
#include <windows.h>

#include <fstream>
#include <string>

#include <eb_xbox_filesystem.hpp>

#include "xbox_game.hpp"
#include "xbox_platform.hpp"

namespace {

namespace fs = eb::xbox::fs;
int g_failures = 0;

void check(const char *name, bool ok) {
    eb::xbox::cout << (ok ? "  [ ok ] " : "  [FAIL] ") << name << '\n';
    if (!ok) ++g_failures;
}

bool write_text(const fs::path &path, const std::string &text) {
    std::ofstream out(path.string(), std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    return static_cast<bool>(out);
}

std::string read_text(const fs::path &path) {
    std::ifstream in(path.string(), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void storage_self_test() {
    const fs::path root("E:\\EarthBound\\selftest.d");
    std::error_code ec;

    fs::remove(root / "save.srm", ec);
    fs::remove(root / "save.tmp", ec);
    fs::remove(root / "save.srm.eb_old", ec);

    check("create_directories", fs::create_directories(root, ec) || fs::is_directory(root));
    check("is_directory", fs::is_directory(root));

    check("write save.srm (v1)", write_text(root / "save.srm", "version-1"));
    check("exists + file_size", fs::exists(root / "save.srm") && fs::file_size(root / "save.srm") == 9);

    // The path the game uses to persist saves: temporary file, then replace the old one.
    check("write save.tmp (v2)", write_text(root / "save.tmp", "version-2!"));
    fs::rename(root / "save.tmp", root / "save.srm", ec);
    check("rename replaces existing file", !ec);
    check("new contents are in place", read_text(root / "save.srm") == "version-2!");
    check("temporary file is gone", !fs::exists(root / "save.tmp"));
    check("parked copy is gone", !fs::exists(root / "save.srm.eb_old"));

    std::size_t seen = 0;
    bool found = false;
    for (const auto &entry : fs::directory_iterator(root)) {
        ++seen;
        found = found || entry.path().filename() == fs::path("save.srm");
    }
    check("directory_iterator finds save.srm", seen >= 1 && found);

    check("equivalent is case-insensitive",
          fs::equivalent(fs::path("E:\\EarthBound\\SAVE.SRM"), fs::path("e:/earthbound/save.srm"), ec));
    check("remove", fs::remove(root / "save.srm", ec) && !fs::exists(root / "save.srm"));
}

} // namespace

int main() {
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    eb::xbox::set_screen_console_enabled(true);

    eb::xbox::cout << "EarthBound (Phase Distorter) - Xbox port\n\n";

    const bool mounted = nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\");
    check("mount E: (Harddisk0\\Partition1)", mounted);
    if (!mounted) {
        eb::xbox::cout << "\nCannot mount E:, which holds the asset pack and saves.\n";
        for (;;) Sleep(1000);
    }
    std::error_code ec;
    fs::create_directories(fs::path("E:\\EarthBound"), ec);

    // Create an empty file E:\EarthBound\selftest to run the storage checks on next boot.
    if (fs::exists(fs::path("E:\\EarthBound\\selftest"), ec)) {
        eb::xbox::cout << "\nStorage layer:\n";
        storage_self_test();
        eb::xbox::cout << '\n' << (g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED") << '\n';
        Sleep(4000);
    }

    eb::xbox::run_game();
}
