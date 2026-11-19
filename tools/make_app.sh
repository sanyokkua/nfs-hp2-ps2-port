#!/bin/bash
# Developer's app: build/release/hp2 (tools/build_release.sh) -> build/app/NFS Hot Pursuit 2.app. It holds the game's code compiled from your disc:
# for your own Mac only. Players get the installer (tools/make_dmg.sh), which builds this on their Mac.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
BUILD="${1:-build/release}"; OUT="${2:-build/app}"
mkdir -p "$OUT"
tools/make_icon.sh "$OUT/hp2.icns"
mac/assemble_app.sh "$BUILD/hp2" build/deps/lib "$OUT/hp2.icns" "$OUT/NFS Hot Pursuit 2.app" "$(cat VERSION)"
rm -f "$OUT/hp2.icns"
echo "app: $OUT/NFS Hot Pursuit 2.app"
