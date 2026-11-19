// NFS Hot Pursuit 2. GPL-3.0-or-later.
// The player's folder (player.cpp), laid out as the NFS Underground ports':
//   ~/Library/Application Support/NFS Hot Pursuit 2/
//     game/     the disc's files and the port's data files (the installer puts them there)
//     player/   the working directory: hp2.ini
//     saves/    the memory card (save.dat), as one PS2 card
//     logs/     hp2.log (+ .1 .. .5 from earlier runs), install.log
#pragma once
#include <string>

// the folder above (HP2_USER_DIR instead when set), made if missing; ends with '/'
std::string user_dir();

// Sets the folder up for this run: the subfolders, an older layout moved over once (hp2.ini at the top, PC Profiles' saves/<name>/),
// HP2_MEMCARD (one card: saves/), cwd = player/, and the log to logs/hp2.log when not started from a terminal.
// Returns the game's folder (NFSHP2/ and data/): HP2_DATA when set, else game/. Empty: not installed (the reason was shown).
std::string player_setup();
