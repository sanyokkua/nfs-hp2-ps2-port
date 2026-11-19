# Third-party notices

NFS Hot Pursuit 2 for Mac (GPL-3.0-or-later, `LICENSE`) is built on the work below. The installer ships each one's license text in
`Contents/Resources/kit/licenses/`.

| Project | What it gives | Where | License |
|---|---|---|---|
| [nfshp2-port-win](https://github.com/ZloiKILLER/nfshp2-port-win) by ZloiKILLER | the recompiler (`gen.py`, `gen_iop.py`), the PS2 runtime, the renderer, the menu hooks | `engine/` (how it was made: `docs/upstream-patches/`), source in the installer's kit | no license file upstream; used here under this project's GPL-3.0-or-later, with credit (see `NOTICE`) |
| [nfshp2-web](https://github.com/vitorinii/nfshp2-web) by Vitorini | the POSIX host files (`engine/src/mac/web/`, `window_host.cpp`, `pad_host.cpp`), the software EE float | `engine/src/mac/web/`, `docs/upstream-patches/0001` | MIT |
| [PCSX2](https://github.com/PCSX2/pcsx2) | the EE's float model that upstream's `eefloat.h` follows | upstream's sources | GPL-3.0-or-later |
| [NFSU2Deadware](https://github.com/LeoDeadware/NFSU2Deadware) by LeoDeadware | the installer's pattern: CLT check, progress window, no-game-code test | `installer/`, `tests/dmg_no_game_code.sh` | MIT |
| [SMAA](https://github.com/iryoku/smaa) by Jorge Jimenez et al. | the renderer's SMAA anti-aliasing (upstream's) | `engine/recomp/runtime/smaa/`, `shaders/smaa/` | MIT |
| [Dear ImGui](https://github.com/ocornut/imgui) 1.92.9 by Omar Cornut | draws the settings overlay | `engine/third_party/imgui/` | MIT |
| [Roboto](https://fonts.google.com/specimen/Roboto) by Google | the settings overlay's font | `engine/third_party/fonts/` | Apache-2.0 |
| [SDL3](https://github.com/libsdl-org/SDL) 3.4.16 | window, input, gamepads, sound | `libSDL3.0.dylib`, headers | Zlib |
| [MoltenVK](https://github.com/KhronosGroup/MoltenVK) | Vulkan on Metal | `libMoltenVK.dylib` | Apache-2.0 |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) 1.4.357.0 | Vulkan's headers | headers | Apache-2.0 / MIT |
| [glslang](https://github.com/KhronosGroup/glslang) 16.6.0 | compiles the shaders (on the developer's Mac only; the SPIR-V ships in the kit) | build tool | BSD-3-Clause and others |
