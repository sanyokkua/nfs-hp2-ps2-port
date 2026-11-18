#!/bin/bash
# The libraries and headers the game is built with, for macOS 15 and later, into build/deps (or $1):
#   lib/libSDL3.0.dylib       SDL3, built from its pinned, SHA-256-checked source (Homebrew's is built for the newest macOS only)
#   lib/libMoltenVK.dylib     MoltenVK, Homebrew's (built for macOS 12+)
#   include/SDL3, include/vulkan, include/vk_video   the headers
#   share/licenses/<name>/    their license files
# Adapted from SpeedBreaker's scripts/macos/build_deps.sh. Needs cmake, ninja, curl and Homebrew's molten-vk.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="$(mkdir -p "${1:-$ROOT/build/deps}" && cd "${1:-$ROOT/build/deps}" && pwd -P)"
TARGET=15.0
DOWNLOADS="$PREFIX/downloads"
mkdir -p "$DOWNLOADS" "$PREFIX/lib" "$PREFIX/include" "$PREFIX/share/licenses"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/hp2-deps.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

# name | URL | SHA-256
SOURCES=(
  "SDL3|https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz|7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68"
  "Vulkan-Headers|https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.4.357.0.tar.gz|e87dce08116151f6b6d7de6b6faf41498e87e6cf848ff16fa3bd5402190ad4a3"
)
field() { local IFS='|'; local a=($1); echo "${a[$2]}"; }

# unpack <name>: the source folder, from the checked archive
unpack() {
  local s url sum file
  for s in "${SOURCES[@]}"; do [ "$(field "$s" 0)" = "$1" ] && break; done
  url="$(field "$s" 1)"; sum="$(field "$s" 2)"; file="$DOWNLOADS/$1-$(basename "$url")"
  [ -f "$file" ] || { echo "downloading $url" >&2; curl -fsSL -o "$file.part" "$url" && mv "$file.part" "$file"; }
  echo "$sum  $file" | shasum -a 256 -c - >/dev/null || { echo "build_deps.sh: $file isn't the expected archive" >&2; exit 1; }
  mkdir -p "$WORK/$1" && tar -xf "$file" -C "$WORK/$1"
  echo "$WORK/$1/$(ls "$WORK/$1")"
}

echo "== SDL3 (macOS $TARGET)" >&2
src="$(unpack SDL3)"
(cd "$src" && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$WORK/sdl3-install" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=$TARGET -DCMAKE_OSX_ARCHITECTURES=arm64 -DSDL_TESTS=OFF -DSDL_STATIC=OFF \
    -DCMAKE_C_FLAGS_RELEASE="-Os -DNDEBUG" "-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local" \
    -DCMAKE_SHARED_LINKER_FLAGS=-Wl,-headerpad_max_install_names >"$WORK/sdl3.log" 2>&1 \
  && cmake --build build && cmake --install build) >>"$WORK/sdl3.log" 2>&1 || { tail -20 "$WORK/sdl3.log" >&2; exit 1; }
cp -L "$WORK/sdl3-install/lib/libSDL3.0.dylib" "$PREFIX/lib/libSDL3.0.dylib"
install_name_tool -id @rpath/libSDL3.0.dylib "$PREFIX/lib/libSDL3.0.dylib"
rm -rf "$PREFIX/include/SDL3" && cp -R "$WORK/sdl3-install/include/SDL3" "$PREFIX/include/SDL3"
mkdir -p "$PREFIX/share/licenses/SDL3" && cp "$src/LICENSE.txt" "$PREFIX/share/licenses/SDL3/"

echo "== Vulkan-Headers" >&2
src="$(unpack Vulkan-Headers)"
rm -rf "$PREFIX/include/vulkan" "$PREFIX/include/vk_video"
cp -R "$src/include/vulkan" "$src/include/vk_video" "$PREFIX/include/"
mkdir -p "$PREFIX/share/licenses/Vulkan-Headers" && cp "$src/LICENSE.md" "$PREFIX/share/licenses/Vulkan-Headers/"

echo "== MoltenVK (Homebrew)" >&2
MVK="$(brew --prefix molten-vk)"
cp -L "$MVK/lib/libMoltenVK.dylib" "$PREFIX/lib/libMoltenVK.dylib"
install_name_tool -id @rpath/libMoltenVK.dylib "$PREFIX/lib/libMoltenVK.dylib"
mkdir -p "$PREFIX/share/licenses/MoltenVK"
cp "$(brew --prefix)/Cellar/molten-vk/$(brew list --versions molten-vk | awk '{print $2}')/LICENSE" "$PREFIX/share/licenses/MoltenVK/" 2>/dev/null \
  || curl -fsSL -o "$PREFIX/share/licenses/MoltenVK/LICENSE" https://raw.githubusercontent.com/KhronosGroup/MoltenVK/main/LICENSE

# every library must run on macOS $TARGET
for f in "$PREFIX"/lib/*.dylib; do
  m="$(otool -l "$f" | awk '/LC_BUILD_VERSION/{b=1} b&&/minos/{print $2; b=0}' | sort -u | head -1)"
  awk -v m="$m" -v t="$TARGET" 'BEGIN { exit !(m + 0 <= t + 0) }' || { echo "build_deps.sh: $f needs macOS $m (> $TARGET)" >&2; exit 1; }
  echo "   $(basename "$f"): macOS $m+" >&2
done
echo "deps: $PREFIX" >&2
