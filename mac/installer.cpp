// SpeedBreaker HP2. GPL-3.0-or-later.
//
// The first run: the player picks their disc image (.iso), and the game's folder on it (NFSHP2/, 2.9 GB) and its program
// (SLUS_203.62, checked by SHA-1) are copied into ~/Library/Application Support/SpeedBreaker HP2/game, beside the port's own data
// files (upstream's recomp/data: the app's Resources/data). macOS's own bsdtar reads the ISO. Nothing of the game is in the app.
#include "installer.h"

#include <SDL3/SDL.h>
#include <CommonCrypto/CommonDigest.h>

#include <spawn.h>
#include <unistd.h>
#include <sys/wait.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

extern char **environ;
namespace fs = std::filesystem;

namespace {
const char *const kTitle = "Need for Speed: Hot Pursuit 2";
const char *const kProgramSha1 = "6397d4425d55056678ed5a405cc81542f7d4d2ee";

std::string sha1_of(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    CC_SHA1_CTX ctx;
    CC_SHA1_Init(&ctx);
    std::vector<char> buf(1 << 20);
    while (f.read(buf.data(), (std::streamsize)buf.size()) || f.gcount() > 0) CC_SHA1_Update(&ctx, buf.data(), (CC_LONG)f.gcount());
    unsigned char d[CC_SHA1_DIGEST_LENGTH];
    CC_SHA1_Final(d, &ctx);
    char hex[2 * CC_SHA1_DIGEST_LENGTH + 1];
    for (int i = 0; i < CC_SHA1_DIGEST_LENGTH; i++) snprintf(hex + 2 * i, 3, "%02x", d[i]);
    return hex;
}

// the port's data files: the app's Resources/data, or the source tree's (a build run from build/)
fs::path port_data()
{
    const char *base = SDL_GetBasePath();
    if (base && fs::is_directory(fs::path(base) / "data")) return fs::path(base) / "data";
    return fs::path(HP2_SOURCE_DATA);
}

bool installed(const fs::path &game)
{
    std::error_code ec;
    return fs::exists(game / ".installed", ec) && fs::exists(game / "NFSHP2" / "ZZDATA.BIN", ec) && program_supported((game / "data" / "SLUS_203.62").string());
}

// bsdtar -xf <iso> -C <to> NFSHP2 SLUS_203.62, waited for with the window kept alive
bool extract(SDL_Window *window, const std::string &iso, const fs::path &to)
{
    std::atomic<int> result{-1};
    std::thread([&] {
        const std::string dir = to.string();
        const char *argv[] = {"/usr/bin/bsdtar", "-xf", iso.c_str(), "-C", dir.c_str(), "NFSHP2", "SLUS_203.62", nullptr};
        pid_t pid;
        int status = 1;
        if (posix_spawn(&pid, argv[0], nullptr, nullptr, (char *const *)argv, environ) == 0) waitpid(pid, &status, 0);
        result = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }).detach();
    SDL_SetWindowTitle(window, "Installing Need for Speed: Hot Pursuit 2 from your disc image (2.9 GB)...");
    while (result < 0) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_EVENT_QUIT) _exit(0);     // the copy is in game.part: the next start begins again
        SDL_Delay(50);
    }
    SDL_SetWindowTitle(window, kTitle);
    return result == 0;
}

std::string pick_iso(SDL_Window *window)
{
    struct Pick {
        std::atomic<int> done{0};
        std::string path;
    } pick;
    static const SDL_DialogFileFilter filters[] = {{"PlayStation 2 disc image", "iso"}};
    SDL_ShowOpenFileDialog(
        [](void *user, const char *const *list, int) {
            Pick &p = *(Pick *)user;
            if (list && list[0]) p.path = list[0];
            p.done = 1;
        },
        &pick, window, filters, 1, nullptr, false);
    while (!pick.done) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_EVENT_QUIT) _exit(0);
        SDL_Delay(16);
    }
    return pick.path;
}

void fail(SDL_Window *window, const std::string &why) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, why.c_str(), window); }
}   // namespace

std::string user_dir()
{
    static const std::string dir = [] {
        const char *home = getenv("HOME");
        fs::path p = fs::path(home ? home : ".") / "Library" / "Application Support" / "SpeedBreaker HP2";
        std::error_code ec;
        fs::create_directories(p, ec);
        return p.string() + "/";
    }();
    return dir;
}

bool program_supported(const std::string &path) { return sha1_of(path) == kProgramSha1; }

std::string install_ensure(SDL_Window *window)
{
    if (const char *data = getenv("HP2_DATA")) return data;
    const fs::path game = fs::path(user_dir()) / "game", part = fs::path(user_dir()) / "game.part";
    if (installed(game)) return game.string();
    const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose Disc Image..."},
                                                {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit"}};
    const SDL_MessageBoxData box = {SDL_MESSAGEBOX_INFORMATION, window, kTitle,
                                    "The game's files are not installed yet.\n\nChoose your disc image of Need for Speed: Hot Pursuit 2 "
                                    "(PlayStation 2, USA, SLUS-20362). Its files are copied to ~/Library/Application Support/"
                                    "SpeedBreaker HP2 (2.9 GB, about a minute).",
                                    2, buttons, nullptr};
    // HP2_INSTALL_ISO=<image>: that image, no questions (tests, scripts); one try
    const char *scripted = getenv("HP2_INSTALL_ISO");
    for (bool first = true;; first = false) {
        if (scripted && !first) return "";
        int choice = 1;
        if (!scripted && (!SDL_ShowMessageBox(&box, &choice) || choice != 1)) return "";
        const std::string iso = scripted ? scripted : pick_iso(window);
        if (iso.empty()) continue;
        std::error_code ec;
        fs::remove_all(part, ec);
        fs::create_directories(part / "data", ec);
        if (!extract(window, iso, part) || !fs::exists(part / "SLUS_203.62", ec) || !fs::is_directory(part / "NFSHP2", ec)) {
            fail(window, "That file is not a disc image of Hot Pursuit 2 for the PlayStation 2 (no NFSHP2 folder or SLUS_203.62 on it).");
            continue;
        }
        fs::rename(part / "SLUS_203.62", part / "data" / "SLUS_203.62", ec);
        if (!program_supported((part / "data" / "SLUS_203.62").string())) {
            fail(window, "This disc's program is not the version the port is made for.\n\nSupported: USA SLUS-20362 v1.00 "
                         "(Europe v1.00 has the same program).");
            continue;
        }
        fs::copy(port_data(), part / "data", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) {
            fail(window, "Copying the port's data files failed: " + ec.message());
            return "";
        }
        // the disc's files come out read-only
        for (auto it = fs::recursive_directory_iterator(part, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            fs::permissions(it->path(), fs::perms::owner_write, fs::perm_options::add, ec);
        std::ofstream(part / ".installed") << iso << "\n";
        fs::remove_all(game, ec);
        fs::rename(part, game, ec);
        if (ec || !installed(game)) {
            fail(window, "The game's files could not be put in place: " + ec.message());
            return "";
        }
        return game.string();
    }
}
