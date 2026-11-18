# Changelog

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
