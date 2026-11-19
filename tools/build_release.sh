#!/bin/bash
# The release build (build/release): the game's code as C++ from game/extracted (once, tools/gen.sh), then hp2 and hp2_headless
# against build/deps for macOS 15+. Its compiled shaders go into the installer's kit (tools/make_dmg.sh).
# Needs: tools/build_deps.sh, tools/install_disc.sh <your .iso>.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
export PATH="/opt/homebrew/bin:$PATH"
[[ -f engine/work/recomp/gen/functable.cpp ]] || tools/gen.sh
cmake -S engine -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build/release
echo "release: build/release/hp2 (and hp2_headless)"
