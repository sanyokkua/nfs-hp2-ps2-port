#!/bin/bash
# Your disc image (.iso, USA SLUS-20362) into game/extracted, as the installer does it for players (mac/import_disc.py):
# NFSHP2/ as on the disc, data/ = the program + the port's data files.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
ISO="${1:?usage: tools/install_disc.sh <disc image .iso>}"
mkdir -p game
python3 -I mac/import_disc.py "$ISO" game/extracted engine/recomp/data
echo "game: game/extracted ($(du -sh game/extracted | cut -f1))"
