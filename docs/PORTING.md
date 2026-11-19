# How the port was made

Step by step, with what was checked at each step. For the shape of the repository and the build, see the README.

## Sources (pinned)
| What | Where | Pin |
|---|---|---|
| [ZloiKILLER/nfshp2-port-win](https://github.com/ZloiKILLER/nfshp2-port-win): the recompiler and the PS2 runtime. No license file upstream; used and bundled here under this project's GPL-3.0-or-later, with credit (`NOTICE`) | `engine/` ([how it was made](upstream-patches/README.md)) | 0.14 `da838179` (section 1 used 0.13 `8ae05937`) |
| [vitorinii/nfshp2-web](https://github.com/vitorinii/nfshp2-web) (MIT): the POSIX/arm64 reference | `engine/src/mac/web/`, `upstream-patches/0001` | its main at clone time; its patch targets upstream 0.13 |

Disc: `SLUS_203.62` SHA-1 `6397d4425d55056678ed5a405cc81542f7d4d2ee`, SHA-256
`7335b8ed479481082c648958f4b5be4da65b74d042d0ab2f5237df801eafacc2`. The game folder (`HP2_DATA`): `NFSHP2/` as on the disc and
`data/` = `SLUS_203.62` + `engine/recomp/data/` (`mac/import_disc.py`; for developers `game/extracted`, `tools/install_disc.sh`).

## 1. A headless arm64 build that is bit-exact (2026-11-18)
- `gen.py`: 9,433 functions (+951 leaf entries), 25 files, 226 jump-table functions, 11 s. `gen_iop.py`: STREAM.IRX, 28 functions.
- `tools/demo_check.sh <hp2_headless>`: all 5 attract recordings replay with 0 differing world checksums (5125, 3125, 3170, 3927,
  2980 steps), 60 game frames/s headless. Test switches: `HP2_PROFILE=test` (skips PC Profiles), `HP2_KEYSCRIPT` (frame-timed
  Enter through the language and title screens), `HP2_DEMOREPLAY=n`, `HP2_DEMOQUIT=1`, `HP2_DIAG=1`. This gate guards every later step.

## 2. The game in a Mac window on MoltenVK (2026-11-18)
- **Rebase onto 0.14:** nfshp2-web's patch applied 3-way; `window_host.cpp` conflicts taken from the patch, plus 0.14's monitor
  API as a one-screen version. 0.14 gaps: `__rdtsc` on arm64 (`__builtin_readcyclecounter`), `stall.h` in `profile.cpp`,
  `phase.cpp` and `cores.cpp` added to the build. Result: `upstream-patches/0001`.
- **Host (`engine/src/mac/host_sdl.cpp`):** SDL3 owns the main thread (AppKit); the game runs on a 64 MB-stack thread through
  `hp2_game_main` (`ps2.cpp` with `HP2_HOST_MAIN`). The host fills nfshp2-web's input block (`host_input.h`): keys as Windows
  virtual keys, mouse, two SDL gamepads (XInput mask, PlayStation detection, rumble), view size. Cmd+Enter / Cmd+F: full screen.
- **Sound (`audio_sdl.cpp`):** an SDL3 audio stream pulling the SPU2 at 48 kHz.
- **Renderer (`native_vk.cpp`):** MoltenVK from `@executable_path/../Frameworks` (then Homebrew, then a Vulkan loader with
  `VK_KHR_portability_enumeration`), `VK_EXT_metal_surface` on the SDL Metal view's layer, `VK_KHR_portability_subset`, the
  window size and refresh rate from the host. Shaders: `glslangValidator -x` wrapped in braces (`engine/src/mac/spv_wrap.cmake`).
- **Exit saves:** `window_quit` just did `_exit`; it now stops the motors and calls `pc_save_on_exit` first, as Windows does on
  close (`upstream-patches/0002`).
- **Checked:** the demo gate on 0.14; a windowed run on the M1 Pro (title, an attract race: car, track, sky, HUD, mirror).

## 3. The installer: the game is built on the player's Mac (v0.1.0, 2026-11-18)
NFSU2Deadware's model, not SpeedBreaker's: the DMG holds no game code or data.
- **DMG** (`tools/make_dmg.sh`): `NFS Hot Pursuit 2 Installer.app` = `installer/install.sh` + a kit: `engine/`'s sources (no
  generated code), compiled SPIR-V shaders, SDL3 built for macOS 15 (`tools/build_deps.sh`), MoltenVK, headers, licenses.
- **Installer:** Command Line Tools (`installer/clt.sh`), the disc (`mac/import_disc.py`: bsdtar + SHA-1), `mac/build_game.sh`
  (CLT `/usr/bin/python3` 3.9 runs the generator, whose output is byte-identical to Python 3.14's; plain clang++ with `xargs -P` on
  the performance cores; sources from `engine/src/mac/sources.txt`, shared with CMake), `mac/assemble_app.sh`, then
  `/Applications/NFS Hot Pursuit 2.app`. A JXA progress window (`installer/progress.js`); Play / Rebuild when run again.
- **Checks:** `tests/dmg_no_game_code.sh` (disc file names and SHA-1s, generated code, 64 windows of the program's `.text`, this
  Mac's paths; a DMG with 1.2 MB of the program fails with 46 windows) and `tools/publish_check.sh`. Install from the DMG with the
  real ISO in a throwaway HOME: 40 s on the M1 Pro.

## 4. The same shape as the NFS Underground ports (0.2.0, 2026-11-19)
The user's ports share one layout, release model and settings menu (NFSU2 is the reference).
- **Self-contained repository:** upstream is in `engine/` (from `git archive` of the old `upstream/` branch `mac`, without SDL3's
  15 MB tarball and the Windows PGO profile); `tools/fetch.sh`, `patches/` and the mirror bundle are gone, the patches are a record
  in `docs/upstream-patches/`. The Mac host moved to `engine/src/mac/`, CMake to `engine/CMakeLists.txt` (`build/release`,
  `tools/build_release.sh`).
- **One disc import** (`mac/import_disc.py`, exit codes 0/3/4/5 as NFSU2's) for the installer and `tools/install_disc.sh`; the
  app's own first-run installer (`installer.cpp`) is gone: the app says to run the installer when `game/` is missing.
- **Player folder** (`engine/src/mac/player.cpp`): `game/`, `player/` (cwd: `hp2.ini`), `saves/` (the memory card), `logs/`
  (`hp2.log` + `.1`-`.5`). One memory card as on a PS2: `HP2_MEMCARD=<folder>/saves` turns PC Profiles off. 0.1.0's layout is
  moved once: `hp2.ini` into `player/`, the last profile's `saves/<name>/save.dat` copied to `saves/save.dat` (the others stay).
  Exit's save then goes to that card (`pc_profile_loaded` knew only profiles).
- **Scripts as NFSU2's:** `build_game.sh <kit> <game> <frameworks> <work> <out> [progress]` (it runs the generator itself),
  `assemble_app.sh <bin> <libs> <icon> <app> <ver>`, `tools/play.sh` (own player folder, `HP2_USER_DIR`), `make_dmg.sh` with
  NOTICE and `SOURCE.txt` in the kit.
- **Checked:** the demo gate on the moved tree: 5 of 5.

## 5. The PS2's own menus, the settings in an overlay (0.2.0, 2026-11-19)
Upstream rewrote the menus for the PC: a nine-row Options with its own pages, Master Volume, Display renamed HUD, Calibrate
Display and the DualShock pictures gone, a PC Profiles screen, a centred pause menu. The user wants the PS2's look, with every
extra in the overlay that the other ports have.
- **One switch:** `pc_ps2_menus()` (`pcmenu.cpp`, on unless `HP2_PC_MENUS=1`). Every upstream menu edit is a `hook` in
  `hle_names.txt` or a change made to the screens in memory as `FRONTB` loads (`port_edits`), so off means the hook calls the
  game's own function. Off: the Options rewrite and its pages, Audio's Master Volume, the Back/Delete hints, the HUD rename and
  Calibrate's removal, the controller pictures, the pause menu's centring, the forced language screen and the Options help-line
  texts (`kText` 100004, 100021-100026), the Cheat Menu's F8 pages. Kept: the main menu's **Exit** row (user's choice), the
  memory card's quick screens (no waits, "Saved" in the corner), the button icons drawn for the keyboard and Xbox pads, typing in
  name entry, the mouse in menus, every non-menu hook.
- **Keys:** the menus' Circle (help) moved from F1 to Tab: F1 opens the overlay, as in the other ports.
- **Overlay:** NFSU2's `engine/src/overlay/` design on Vulkan (see the README's settings table); every row reads and writes
  `g_cfg` and saves `hp2.ini`. Guest calls (language, cheats) run on the game's thread.
- **Checked:** the demo gate with the PS2 menus on: 5 of 5 (its key script lost one Enter: the port's forced language screen is
  gone). Frame dumps of the installed app: the PS2's Main Menu (Hot Pursuit, World Racing, Options) with Exit below, the Exit
  question, the PS2's Options (Audio, Controller, Display, Save/Load, Player Names, Credits), help shown as Tab. Overlay shots
  (`HP2_OVERLAY_SCRIPT`/`HP2_OVERLAY_SHOT`): pages, a change saved to `hp2.ini`, language and cheats run on the game's thread,
  full screen on and off with the menu open, Quit Game exits through the save. Install from the DMG with the real ISO: 40 s; a copy
  of a 0.1.0 folder (profile `Oleksandr`) moved to the new layout with that profile's save as the card.
- **Overlay design:** NFSU2's `overlay.mm` split into `overlay_core.cpp` (the same code, SDL3) and `overlay_vk.cpp` (a render pass
  with LOAD on the swapchain image after the picture and the FPS counter, so the swapchain also has COLOR_ATTACHMENT and
  TRANSFER_SRC usage; nothing runs while the menu is closed). The font is compiled in (`overlay_font.inc`: CMake, or `xxd -i` in
  `build_game.sh`). `pc_config_save` holds a lock: the render thread and the game's both save `hp2.ini`.

## Pitfalls (each cost a run)
- **Symlinked game folder:** `disc.cpp`'s `recursive_directory_iterator` doesn't follow a symlinked `NFSHP2`; use APFS clones
  (`cp -Rc`).
- **CLT clang can't find the SDK** when called directly: pass `-isysroot $(xcrun --show-sdk-path)`.
- **`ls` + `pipefail`** kills a script when no objects exist yet: use `find`.
- **Spaces in paths** (the installer app's name): tab-separated job lines read with `IFS=$'\t' read`, flags through a file.
- **The no-game-code test:** compare only the disc's own files (`data/` also holds upstream's); check for `$HOME`, not "/Users/"
  (SDL headers have it in a comment); skip `__pycache__`.
- **Dev runs wrote `hp2.ini` and `saves/` into the repository** (and one was released in 0.1.0): use `tools/play.sh`.
- **The demo gate needs the headless build:** in a real window scripted Enter presses land in other menus (timing). Check windowed
  builds with frame dumps (`HP2_NATIVESHOT=<path%03d.bmp> HP2_NATIVESHOTN=<frames>`).
- **`HP2_MEMCARD` turns profiles off, and Exit's save with them** unless the card's folder is used (`mc_card_dir`).
- **`build/deps/lib/libMoltenVK.dylib` copied from Homebrew can carry a broken signature:** dyld kills `build/release/hp2` (137) when
  it loads it. Re-sign it (`codesign -f -s -`); the app's copy is signed by `assemble_app.sh`.
