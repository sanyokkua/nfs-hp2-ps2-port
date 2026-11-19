# How engine/ was made

`engine/` is [ZloiKILLER/nfshp2-port-win](https://github.com/ZloiKILLER/nfshp2-port-win) at `da838179` (release 0.14) with these
patches applied in order (`git am`), imported on 2026-11-19 so that the port builds from this repository alone. Later changes to
`engine/` are in this repository's git history.

- `0001`: the POSIX host files from [nfshp2-web](https://github.com/vitorinii/nfshp2-web) by Vitorini (MIT: `window_host.cpp`,
  `pad_host.cpp`, the soft EE floats), 0.14's portability fixes, MoltenVK's loader and Metal surface in `native_vk.cpp`, and
  `main` as `hp2_game_main` for the macOS host.
- `0002`: Exit saves the profile and stops the motors first, as the Windows window's close does.

Left out of the import: `third_party/SDL3-3.4.18.tar.gz` (15 MB; `tools/build_deps.sh` builds SDL3 for macOS 15+) and
`recomp/pgo/speed.profdata` (the Windows build's profile; the Mac builds don't use it).

The Mac's own code is beside upstream's, in `engine/src/` (the host, the player folder, the settings overlay) and
`engine/third_party/` (Dear ImGui, the font). The changes after the import to upstream's files (the PS2 menus switch,
`pc_ps2_menus()`, and its uses) are in this repository's history.
