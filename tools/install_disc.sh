#!/bin/bash
# Your disc image (.iso, USA SLUS-20362) into game-hp2/files: NFSHP2/ as on the disc, data/ = the program + upstream's data files.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
ISO="${1:?usage: tools/install_disc.sh <disc image .iso>}"
OUT=game-hp2/files
mkdir -p "$OUT/data"
bsdtar -xf "$ISO" -C "$OUT" NFSHP2 SLUS_203.62
chmod -R u+w "$OUT"
mv -f "$OUT/SLUS_203.62" "$OUT/data/SLUS_203.62"
cp -R upstream/recomp/data/. "$OUT/data/"
echo "game: $OUT ($(du -sh "$OUT" | cut -f1))"
