// NFS Hot Pursuit 2. GPL-3.0-or-later.
// The player's folder and the game's files in it (installer.cpp).
#pragma once
#include <string>

struct SDL_Window;

// ~/Library/Application Support/NFS Hot Pursuit 2/ (hp2.ini, saves/, logs/, game/), made if missing; ends with '/'
std::string user_dir();

// The game's folder (NFSHP2/ and data/): HP2_DATA when set, else user_dir()/game, installed from the player's disc image first
// when it is not there yet. On the main thread, with the window up (it shows the file dialog and pumps events while it copies).
// Empty: no game (the player cancelled, or the disc is not the supported one; the reason was shown).
std::string install_ensure(SDL_Window *window);

// The disc's program is the one the port is built for: USA SLUS-20362 v1.00 (same as Europe v1.00)
bool program_supported(const std::string &path);
