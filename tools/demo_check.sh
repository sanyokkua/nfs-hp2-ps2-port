#!/bin/bash
# Determinism gate: replays the game's 5 attract-demo recordings headless and checks that every world step's
# checksum matches the recording (the game's own World::VerifyDebugChecksums).
#   tools/demo_check.sh <hp2 binary> [demo numbers...]   (default: 1 2 3 4 5, run in parallel)
# Needs game/extracted (NFSHP2/ + data/ with SLUS_203.62). Uses a throwaway HOME per demo.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; shift
DEMOS=("$@"); [ $# -eq 0 ] && DEMOS=(1 2 3 4 5)
WORK="$(mktemp -d "${TMPDIR:-/tmp}/hp2demo.XXXXXX")"
# Enter through the title (and, with HP2_PC_MENUS=1, the port's language screen first), then idle in the main menu
if [ -n "${HP2_PC_MENUS:-}" ]; then printf '600 0d 6\n900 0d 6\n1200 0d 6\n'; else printf '600 0d 6\n900 0d 6\n'; fi > "$WORK/keys.txt"
for d in "${DEMOS[@]}"; do
  ( mkdir -p "$WORK/home$d"; cd "$WORK/home$d"
    HOME="$WORK/home$d" HP2_KEYSCRIPT="$WORK/keys.txt" HP2_PROFILE=test HP2_DATA="$ROOT/game/extracted" \
    HP2_DEMOREPLAY=$d HP2_DEMOQUIT=1 HP2_DIAG=1 timeout 480 "$BIN" > "$WORK/d$d.log" 2>&1 || true ) &
done
wait
fail=0
for d in "${DEMOS[@]}"; do
  line=$(grep -E 'steps in all' "$WORK/d$d.log" | tail -1 || true)
  if [[ "$line" =~ \ 0\ differ$ ]]; then echo "demo $d: PASS ($line)"; else echo "demo $d: FAIL (${line:-no result}) log $WORK/d$d.log"; fail=1; fi
done
[ $fail -eq 0 ] && rm -rf "$WORK"
exit $fail
