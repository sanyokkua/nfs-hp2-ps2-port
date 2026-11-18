# SpeedBreaker HP2: porting log

## Sources (pinned)
| What | Where | Pin |
|---|---|---|
| ZloiKILLER/nfshp2-port-win (no license: private use only) | `upstream/`, backup `mirror/nfshp2-port-win.bundle` | 0.14 `da838179`; P1 used 0.13 `8ae05937` |
| vitorinii/nfshp2-web (MIT), reference for POSIX/arm64 | `reference/nfshp2-web/` | its main at clone time; its patch targets upstream 0.13 |

Disc: `SLUS_203.62` SHA-1 `6397d4425d55056678ed5a405cc81542f7d4d2ee`, SHA-256 `7335b8ed479481082c648958f4b5be4da65b74d042d0ab2f5237df801eafacc2`.
Layout for `HP2_DATA`: `game-hp2/files/{NFSHP2/, data/}` (`data/` = `SLUS_203.62` + upstream `recomp/data/`).

## P1 result (2026-11-18): headless arm64 build is bit-exact
- `gen.py`: 9,433 functions (+951 leaf entries), 25 files, 226 jump-table functions, 11 s. `gen_iop.py`: STREAM.IRX, 28 functions.
- `make native` (nfshp2-web Makefile, Apple clang) builds `build/native/hp2`; `make test`: eefloat_soft 0 mismatches.
- `tools/demo_check.sh <hp2>`: all 5 attract recordings replay with 0 differing world checksums
  (5125, 3125, 3170, 3927, 2980 steps). Runs at 60 game frames/s headless.
- Test switches used: `HP2_PROFILE=test` (skips PC Profiles), `HP2_KEYSCRIPT` (frame-timed Enter through the
  language and title screens), `HP2_DEMOREPLAY=n`, `HP2_DEMOQUIT=1`, `HP2_DIAG=1`.

## P2/P3 result (2026-11-18): the game runs in a Mac window on MoltenVK
Upstream is now 0.14 (`da838179`); our changes are `patches/0001-*.patch` (from `upstream` branch `mac`).
- **Rebase:** nfshp2-web's patch 3-way applied; `window_host.cpp` conflicts taken from the patch, plus 0.14's new monitor API as a
  one-screen version. 0.14 gaps fixed: `__rdtsc` on arm64 (`__builtin_readcyclecounter`), `stall.h` in `profile.cpp`, `phase.cpp` and
  `cores.cpp` added to the build (both have non-Windows branches).
- **Host (`mac/host_sdl.cpp`):** SDL3 owns the main thread (AppKit); the game runs on a 64 MB-stack thread through `hp2_game_main`
  (`ps2.cpp` built with `HP2_HOST_MAIN`). The host fills nfshp2-web's input block (`host_input.h`): keys as Windows virtual keys, mouse,
  two SDL gamepads (XInput mask, PlayStation detection, rumble), view size. Cmd+Enter / Cmd+F toggles fullscreen.
- **Sound (`mac/audio_sdl.cpp`):** SDL3 audio stream pulling the SPU2 at 48 kHz.
- **Renderer (`native_vk.cpp`):** loads MoltenVK (`@executable_path/../Frameworks`, then Homebrew; a Vulkan loader as fallback with
  `VK_KHR_portability_enumeration`), `VK_EXT_metal_surface` on the SDL Metal view's layer, `VK_KHR_portability_subset` enabled,
  window size and refresh rate from the host. Shaders: `glslangValidator -x` wrapped in braces (`tools/spv_wrap.cmake`).
- **Checked:** `ctest` (float test + all 5 demos bit-exact on 0.14); windowed run on the M1 Pro: title screen and an attract race
  render correctly (car, track, sky, HUD, rear-view mirror) at the race's default 30 fps; frame dumps with `HP2_NATIVESHOT`.

## Build from scratch
```sh
tools/fetch.sh                       # upstream/ at the pin + patches (GitHub, or mirror/ bundle)
tools/install_disc.sh <your .iso>    # game-hp2/files
tools/gen.sh                         # the game's code as C++
cmake -S . -B build/mac -G Ninja -DHP2_DEMO_TESTS=ON && cmake --build build/mac && ctest --test-dir build/mac
HP2_DATA=$PWD/game-hp2/files build/mac/hp2
```
Needs Homebrew `sdl3 molten-vk vulkan-headers glslang`.

## Next
- Not yet checked by a person: keyboard/gamepad feel, sound, fullscreen, saves, 60 fps setting, performance at higher scale.
- P4: SpeedBreaker shell (installer from the ISO, settings menu, `.app` bundle with MoltenVK and SDL inside).
