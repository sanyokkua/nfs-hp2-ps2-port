#!/bin/bash
# Builds the game's binary on the player's Mac from their own disc (the installer carries no generated game code; Apple's Command
# Line Tools only, no Homebrew, no cmake):
#   mac/build_game.sh <kit> <game dir> <frameworks dir> <work dir> <out binary> [progress file]
# kit (tools/make_dmg.sh): engine/ (the runtime's and the host's sources, the generator), shaders/ (*.spv.h), include/ (SDL3,
# Vulkan headers). game dir: data/SLUS_203.62 and NFSHP2/ (mac/import_disc.py). frameworks: libSDL3.0.dylib (+ libMoltenVK.dylib).
# Steps: the program and the sound driver to C++ (CLT python3: the same C++ as a newer Python's), compile on the performance cores,
# link. Progress lines "N text" (N = 5..95) go to the progress file; objects newer than their source are kept (a re-run resumes).
# HP2_BUILD_JOBS=<n> sets the compile jobs. Same interface as the NFS Underground ports' mac/build_game.sh.
set -euo pipefail
KIT="$1" GAME="$2" FW="$3" WORK="$4" OUT="$5" PROGRESS="${6:-/dev/null}"
say() { echo "$1 $2" >> "$PROGRESS"; echo "build_game: [$1%] $2" >&2; }
die() { echo "build_game: ERROR $1" >&2; exit 1; }
CXX="$(xcrun -f clang++)" || die "no clang++ (Command Line Tools)"
SDK="$(xcrun --sdk macosx --show-sdk-path)"
JOBS="${HP2_BUILD_JOBS:-$(/usr/sbin/sysctl -n hw.perflevel0.physicalcpu 2>/dev/null || /usr/sbin/sysctl -n hw.ncpu)}"
[ -f "$GAME/data/SLUS_203.62" ] || die "no data/SLUS_203.62 in $GAME"
mkdir -p "$WORK" "$(dirname "$OUT")"

# 1. the generated code (gen.py writes beside itself: a copy of the kit's engine/)
SRC="$WORK/engine" GEN="$WORK/engine/work/recomp/gen" OBJ="$WORK/obj"
if [ ! -f "$GEN/.done" ]; then
  say 5 "Translating the game's program to C++"
  rm -rf "$SRC" && cp -R "$KIT/engine" "$SRC"
  /usr/bin/python3 -I "$SRC/tools/recomp/gen.py" --elf "$GAME/data/SLUS_203.62" || die "translating the game's program failed"
  /usr/bin/python3 -I "$SRC/tools/recomp/gen_iop.py" --irx "$GAME/NFSHP2/IRX253/STREAM.IRX" || die "translating the sound driver failed"
  touch "$GEN/.done"
fi
SHADERS="$KIT/shaders" INC="$KIT/include"
FLAGS=(-isysroot "$SDK" -std=c++17 -arch arm64 -mmacosx-version-min=15.0 -w -fno-strict-aliasing -ffp-contract=off
       -include "$SRC/src/mac/web/compat_posix.h" -DHP2_HOST_MAIN -DIMGUI_IMPL_VULKAN_NO_PROTOTYPES
       -I"$SRC/src/mac" -I"$SRC/src/mac/web" -I"$SRC/recomp/runtime" -I"$GEN" -I"$SHADERS" -I"$INC"
       -I"$SRC/src/overlay" -I"$SRC/third_party/imgui" -I"$SRC/third_party/imgui/backends" -I"$OBJ/generated")
mkdir -p "$OBJ/generated"
# the settings menu's font, compiled in: the .ttf's bytes as "0x.., " text (engine/CMakeLists.txt makes the same file)
/usr/bin/xxd -i < "$SRC/third_party/fonts/Roboto-Medium.ttf" > "$OBJ/generated/overlay_font.inc" || die "no font for the settings menu"

# the jobs: "<-O level>\t<extra flag or ->\t<source>" per line (sources may have spaces: the app bundle)
LIST="$OBJ/jobs.txt"; : > "$LIST"
for f in "$GEN"/*.cpp; do printf -- '-O1\t-\t%s\n' "$f" >> "$LIST"; done
grep -v '^#' "$SRC/src/mac/sources.txt" | while read -r f; do
  [ -n "$f" ] || continue
  case "$f" in *native_movie.cpp) extra=-fwrapv ;; *) extra=- ;; esac
  printf -- '-O2\t%s\t%s\n' "$extra" "$SRC/$f" >> "$LIST"
done
n=$(wc -l < "$LIST" | tr -d ' ')

say 10 "compiling $n files on $JOBS cores"
# one object, kept when newer than its source; xargs can't pass an array, so the flags go through a file
printf '%s\n' "${FLAGS[@]}" > "$OBJ/flags.txt"
compile_one() {     # one job line
  local opt extra src o flags=()
  IFS=$'\t' read -r opt extra src <<< "$1"
  while IFS= read -r l; do flags+=("$l"); done < "$OBJ/flags.txt"
  o="$OBJ/$(echo "$src" | sed 's|.*/recomp/runtime/|rt_|; s|.*/src/|src_|; s|.*/third_party/|tp_|; s|.*/gen/|gen_|; s|/|_|g').o"
  [ "$o" -nt "$src" ] && return 0
  [ "$extra" = - ] && extra=
  nice -n 10 "$CXX" "${flags[@]}" $opt $extra -c "$src" -o "$o.part" 2>>"$OBJ/compile.log" && mv "$o.part" "$o"
}
export -f compile_one; export CXX OBJ
: > "$OBJ/compile.log"
( tr '\n' '\0' < "$LIST" | xargs -0 -P "$JOBS" -n 1 bash -c 'compile_one "$0"' ) &
pid=$!
while kill -0 $pid 2>/dev/null; do
  done_n=$(find "$OBJ" -maxdepth 1 -name '*.o' | wc -l | tr -d ' ')
  say $((10 + 80 * done_n / n)) "compiling: $done_n of $n"
  sleep 3
done
wait $pid || { tail -20 "$OBJ/compile.log" >&2; say 100 "ERROR: compiling failed (see $OBJ/compile.log)"; exit 1; }
[ "$(find "$OBJ" -maxdepth 1 -name '*.o' | wc -l | tr -d ' ')" -ge "$n" ] || { say 100 "ERROR: some files did not compile"; exit 1; }

say 92 "linking"
"$CXX" -isysroot "$SDK" -arch arm64 -mmacosx-version-min=15.0 -o "$OUT" "$OBJ"/*.o "$FW/libSDL3.0.dylib" \
  -Wl,-rpath,@executable_path/../Frameworks -Wl,-headerpad_max_install_names 2>"$OBJ/link.log" \
  || { tail -20 "$OBJ/link.log" >&2; say 100 "ERROR: linking failed (see $OBJ/link.log)"; exit 1; }
say 95 "built $OUT"
rm -rf "$SRC"       # the generated code goes; the objects stay for a resumed build until the installer cleans up
