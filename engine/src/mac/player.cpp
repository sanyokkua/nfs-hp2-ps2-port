// NFS Hot Pursuit 2. GPL-3.0-or-later.
//
// The player's folder (player.h). The game's files are put in game/ by the installer (installer/install.sh, mac/import_disc.py); the
// app only checks they are there.
#include "player.h"

#include <SDL3/SDL.h>

#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {
const char *const kTitle = "Need for Speed: Hot Pursuit 2";

bool installed(const fs::path &game)
{
    std::error_code ec;
    return fs::exists(game / "NFSHP2" / "ZZDATA.BIN", ec) && fs::exists(game / "data" / "SLUS_203.62", ec);
}

// hp2.ini's "profile=" (the last PC Profile used), or empty
std::string ini_profile(const fs::path &ini)
{
    std::ifstream f(ini);
    std::string line;
    while (std::getline(f, line))
        if (line.rfind("profile=", 0) == 0) {
            std::string v = line.substr(8);
            while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
            return v;
        }
    return "";
}

// Version 0.1.0 kept hp2.ini at the top and one card per PC Profile in saves/<name>/. Now: player/hp2.ini and one card, saves/save.dat
// (the profile the player last used; the others stay where they are). Done once: nothing happens when the new files exist.
void migrate(const fs::path &top)
{
    std::error_code ec;
    if (fs::exists(top / "hp2.ini", ec) && !fs::exists(top / "player" / "hp2.ini", ec)) {
        fs::rename(top / "hp2.ini", top / "player" / "hp2.ini", ec);
        fprintf(stderr, "player: hp2.ini moved to player/%s\n", ec ? " (failed)" : "");
    }
    const fs::path card = top / "saves" / "save.dat";
    if (fs::exists(card, ec)) return;
    fs::path from;
    const std::string last = ini_profile(top / "player" / "hp2.ini");
    if (!last.empty() && fs::exists(top / "saves" / last / "save.dat", ec)) from = top / "saves" / last / "save.dat";
    else
        for (const auto &e : fs::directory_iterator(top / "saves", ec))
            if (e.is_directory(ec) && fs::exists(e.path() / "save.dat", ec)) {
                from = e.path() / "save.dat";
                break;
            }
    if (from.empty()) return;
    fs::copy_file(from, card, ec);
    fprintf(stderr, "player: memory card from %s%s\n", from.c_str(), ec ? " (failed)" : "");
}

// logs/hp2.log, the earlier runs' as hp2.log.1 .. .5
void log_to_file()
{
    for (int i = 4; i >= 1; i--) rename(("logs/hp2.log." + std::to_string(i)).c_str(), ("logs/hp2.log." + std::to_string(i + 1)).c_str());
    rename("logs/hp2.log", "logs/hp2.log.1");
    if (freopen("logs/hp2.log", "w", stderr)) setvbuf(stderr, nullptr, _IOLBF, 0);
}
}   // namespace

std::string user_dir()
{
    static const std::string dir = [] {
        const char *home = getenv("HOME"), *forced = getenv("HP2_USER_DIR");      // HP2_USER_DIR: tools/play.sh's dev folder
        fs::path p = forced ? fs::path(forced) : fs::path(home ? home : ".") / "Library" / "Application Support" / "NFS Hot Pursuit 2";
        std::error_code ec;
        fs::create_directories(p, ec);
        return p.string() + "/";
    }();
    return dir;
}

std::string player_setup()
{
    const fs::path top = user_dir();
    std::error_code ec;
    for (const char *d : {"player", "saves", "logs"}) fs::create_directories(top / d, ec);
    if (chdir(top.c_str()) == 0 && !isatty(2)) log_to_file();     // started from Finder
    migrate(top);
    if (!getenv("HP2_MEMCARD")) setenv("HP2_MEMCARD", (top / "saves").c_str(), 1);    // one card, no PC Profiles
    if (chdir((top / "player").c_str()) != 0) fprintf(stderr, "player: no folder %s\n", (top / "player").c_str());
    if (const char *data = getenv("HP2_DATA")) return data;
    const fs::path game = top / "game";
    if (installed(game)) return game.string();
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle,
                             "The game's files are not installed.\n\nOpen \"NFS Hot Pursuit 2 Installer\" (from the download's disk image) "
                             "and choose your disc image: it puts them in ~/Library/Application Support/NFS Hot Pursuit 2/game.",
                             nullptr);
    return "";
}
