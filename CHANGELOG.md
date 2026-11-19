# Changelog

## 0.2.0 (2026-11-19)

- **The PS2's menus:** Options, Audio, Display, the Controller screens and the pause menu look and work as on the PS2 (nfshp2-port-win's
  PC menus are off). The main menu keeps its **Exit** row; saving to the memory card has no waits.
- **Settings overlay** over the game, as in the NFS Underground ports: **F1**, **Cmd+,** or **Back + Start**. Display, Graphics,
  Audio, Controls and Game pages with every setting the PC menus had, and Quit. In the menus, Circle (help) is now **Tab**.
- **Aspect ratio and resolution:** 4:3, your screen's shape, 16:10, 16:9 or 21:9 (black bars where the screen differs); the
  resolution list shows only sizes of that shape that fit your screen; Render Scale says how high the picture is drawn.
- **One memory card** as on a PS2 (`saves/save.dat`), no PC Profiles. 0.1.0's last profile's save becomes the card.
- **Player folder** as the other ports': `player/hp2.ini`, `saves/`, `logs/` (last five runs), `game/`; 0.1.0's files are moved.
- **Repository:** nfshp2-port-win is in `engine/` (no download at build time), scripts and layout as the NFS Underground ports'.

## 0.1.0 (2026-11-18)

The first release: *Need for Speed: Hot Pursuit 2* (PS2, USA SLUS-20362) on Apple Silicon Macs, built on
[nfshp2-port-win](https://github.com/ZloiKILLER/nfshp2-port-win) 0.14 by ZloiKILLER.

- **Installer:** the DMG holds no game code or data. It checks Apple's Command Line Tools, takes your disc image (SHA-1 checked),
  translates the game's program to C++ and compiles it on your Mac (under a minute on an M1 Pro), then puts
  *NFS Hot Pursuit 2* in Applications.
- **macOS host:** an SDL3 window with keyboard, mouse and gamepads (with rumble), SDL3 sound, Vulkan on MoltenVK (Metal),
  Cmd+Enter / Cmd+F full screen, Cmd+Q. Exit, Cmd+Q and closing the window save the profile first.
- **Files:** settings, profiles, logs and the game's files in `~/Library/Application Support/NFS Hot Pursuit 2`.
- **Checked:** the game's 5 recorded demo races replay with every world checksum matching on arm64.
