#!/bin/bash
# The game's program and its sound driver as C++ (upstream/work/recomp/gen), from game-hp2/files (tools/install_disc.sh).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
GAME="${GAME:-$ROOT/game-hp2/files}"
SHA=$(shasum -a 1 "$GAME/data/SLUS_203.62" | cut -d' ' -f1)
[ "$SHA" = 6397d4425d55056678ed5a405cc81542f7d4d2ee ] || { echo "SLUS_203.62 is not USA SLUS-20362 v1.00 ($SHA)"; exit 1; }
python3 -I upstream/tools/recomp/gen.py --elf "$GAME/data/SLUS_203.62"
python3 -I upstream/tools/recomp/gen_iop.py --irx "$GAME/NFSHP2/IRX253/STREAM.IRX"
