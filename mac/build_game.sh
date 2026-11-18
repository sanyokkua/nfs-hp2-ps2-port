#!/bin/bash
# The game's binary from source with Apple's command line tools only (clang++; no cmake): what the installer runs on the
# player's Mac, and what tools/make_dmg.sh tests. The generated code must be there already (gen.py, gen_iop.py).
#   mac/build_game.sh <source root> <generated code> <shader headers> <deps (include/, lib/)> <objects dir> <binary> [progress file]
# Progress lines "N text" (N 0..100) are appended to the progress file. Objects newer than their source are kept (a re-run resumes).
set -euo pipefail
SRC="$1" GEN="$2" SHADERS="$3" DEPS="$4" OBJ="$5" OUT="$6" PROGRESS="${7:-/dev/null}"
say() { echo "$1 $2" >> "$PROGRESS"; echo "build: $2" >&2; }
CXX="$(xcrun -f clang++)"
SDK="$(xcrun --sdk macosx --show-sdk-path)"
JOBS="${HP2_BUILD_JOBS:-$(sysctl -n hw.perflevel0.physicalcpu 2>/dev/null || sysctl -n hw.ncpu)}"
FLAGS=(-isysroot "$SDK" -std=c++17 -arch arm64 -mmacosx-version-min=15.0 -w -fno-strict-aliasing -ffp-contract=off
       -include "$SRC/mac/web/compat_posix.h" -DHP2_HOST_MAIN "-DHP2_SOURCE_DATA=\"$SRC/upstream/recomp/data\""
       -I"$SRC/mac" -I"$SRC/mac/web" -I"$SRC/upstream/recomp/runtime" -I"$GEN" -I"$SHADERS" -I"$DEPS/include")
mkdir -p "$OBJ"

# the jobs: "<-O level>\t<extra flag or ->\t<source>" per line (sources may have spaces: the app bundle)
LIST="$OBJ/jobs.txt"; : > "$LIST"
for f in "$GEN"/*.cpp; do printf -- '-O1\t-\t%s\n' "$f" >> "$LIST"; done
grep -v '^#' "$SRC/mac/sources.txt" | while read -r f; do
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
  o="$OBJ/$(echo "$src" | sed 's|.*/upstream/|up_|; s|.*/mac/|mac_|; s|.*/gen/|gen_|; s|/|_|g').o"
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
"$CXX" -isysroot "$SDK" -arch arm64 -mmacosx-version-min=15.0 -o "$OUT" "$OBJ"/*.o "$DEPS/lib/libSDL3.0.dylib" \
  -Wl,-rpath,@executable_path/../Frameworks -Wl,-headerpad_max_install_names 2>"$OBJ/link.log" \
  || { tail -20 "$OBJ/link.log" >&2; say 100 "ERROR: linking failed (see $OBJ/link.log)"; exit 1; }
say 95 "built $OUT"
