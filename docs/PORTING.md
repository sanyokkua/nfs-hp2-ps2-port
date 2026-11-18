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

## Next
- Rebase the POSIX changes onto upstream 0.14 (the web patch conflicts in `window.cpp`).
- P2/P3: SDL3 window + input, SDL3 audio, MoltenVK surface for `native_vk.cpp`.
