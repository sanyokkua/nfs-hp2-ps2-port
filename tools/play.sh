#!/bin/bash
# Dev play-test: runs the build with a persistent player folder of its own (hp2.ini, the memory card, logs) in
# ~/Library/Application Support/NFS Hot Pursuit 2/dev, and the game from game/extracted. Log: <that>/logs/last.log.
# Extra env (e.g. HP2_OVERLAY_SCRIPT=...) passes through.   tools/play.sh [binary]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-$ROOT/build/release/hp2}"
P="$HOME/Library/Application Support/NFS Hot Pursuit 2/dev"
mkdir -p "$P/logs"
HP2_USER_DIR="$P" HP2_DATA="$ROOT/game/extracted" exec "$BIN" > "$P/logs/last.log" 2>&1
