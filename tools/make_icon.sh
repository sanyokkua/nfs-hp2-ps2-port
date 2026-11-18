#!/bin/bash
# The app's icon from upstream's speed.ico (its largest image) -> <out .icns>
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$1"; T="$(mktemp -d "${TMPDIR:-/tmp}/hp2icon.XXXXXX")"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/i.iconset"
sips -s format png "$ROOT/upstream/recomp/runtime/speed.ico" --out "$T/icon.png" >/dev/null
for s in 16 32 128 256 512; do
  sips -z $s $s "$T/icon.png" --out "$T/i.iconset/icon_${s}x${s}.png" >/dev/null
  sips -z $((s*2)) $((s*2)) "$T/icon.png" --out "$T/i.iconset/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$T/i.iconset" -o "$OUT"
