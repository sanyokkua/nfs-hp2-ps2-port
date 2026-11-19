#!/bin/bash
# The players' DMG: "NFS Hot Pursuit 2 Installer.app" with its kit (engine/'s sources: nfshp2-port-win's runtime and generator with
# this project's changes and the macOS host; compiled shaders; SDL3 + MoltenVK + headers for macOS 15+). No game code and no game data: the installer builds the
# game on the player's Mac from their own disc. Checked by tests/dmg_no_game_code.sh before it is called done.
#   tools/make_dmg.sh            -> build/dmg/NFS-Hot-Pursuit-2-<version>.dmg (+ .sha256)
# Needs: tools/build_deps.sh (build/deps), the release build (build/release, tools/build_release.sh).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
export PATH="/opt/homebrew/bin:$PATH"
VERSION="$(cat VERSION)"
OUT=build/dmg; STAGE="$OUT/stage"; APP="$STAGE/NFS Hot Pursuit 2 Installer.app"; KIT="$APP/Contents/Resources/kit"
DMG="$OUT/NFS-Hot-Pursuit-2-$VERSION.dmg"
[ -z "$(git status --porcelain -- engine)" ] || { echo "engine/ has uncommitted changes: commit them first"; exit 1; }
[ -f build/deps/lib/libSDL3.0.dylib ] || { echo "run tools/build_deps.sh first"; exit 1; }
cmake --build build/release --target hp2_shaders >/dev/null

rm -rf "$OUT" && mkdir -p "$APP/Contents/MacOS" "$KIT/build/shaders" "$KIT/mac" "$KIT/libs" "$KIT/licenses"
# the installer
cp installer/install.sh "$APP/Contents/MacOS/installer" && chmod 755 "$APP/Contents/MacOS/installer"
cp installer/clt.sh installer/progress.js VERSION "$KIT/"
tools/make_icon.sh "$KIT/hp2.icns"
cp "$KIT/hp2.icns" "$APP/Contents/Resources/installer.icns"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>NFS Hot Pursuit 2 Installer</string>
  <key>CFBundleIdentifier</key><string>io.github.sanyokkua.nfshp2.installer</string>
  <key>CFBundleExecutable</key><string>installer</string>
  <key>CFBundleIconFile</key><string>installer</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>LSMinimumSystemVersion</key><string>15.0</string>
</dict></plist>
PLIST
# the kit: engine/'s sources (no generated code: build_game.sh makes it from the player's disc), our scripts, the port's data files,
# compiled shaders, SDL3 + MoltenVK and the headers
rsync -a --exclude work --exclude __pycache__ --exclude headless_stubs.cpp --exclude eefloat_soft_test.cpp engine/ "$KIT/build/engine/"
cp mac/build_game.sh mac/assemble_app.sh mac/import_disc.py "$KIT/mac/"
cp -R engine/recomp/data "$KIT/data"
cp build/release/shaders/*.spv.h "$KIT/build/shaders/"
rsync -a build/deps/include "$KIT/build/"
cp build/deps/lib/libSDL3.0.dylib build/deps/lib/libMoltenVK.dylib "$KIT/libs/"
# licenses and notices
cp -R LICENSES/. "$KIT/licenses/"
cp -R build/deps/share/licenses/. "$KIT/licenses/"
cp LICENSE NOTICE THIRD_PARTY_NOTICES.md "$KIT/"
echo "NFS Hot Pursuit 2 for Mac $VERSION ($(git rev-parse --short HEAD)): the source of everything in this kit is https://github.com/sanyokkua/nfs-hp2-ps2-port/tree/v$VERSION (engine/ = nfshp2-port-win 0.14 da838179 by ZloiKILLER, https://github.com/ZloiKILLER/nfshp2-port-win, with nfshp2-web's patch and this project's changes)" > "$KIT/SOURCE.txt"
codesign --force --sign - "$APP" >/dev/null 2>&1
# the DMG's other files
cp docs/dmg/read-me-first.txt "$STAGE/Read me first.txt"
cp THIRD_PARTY_NOTICES.md "$STAGE/Third-party notices.md"
ln -s /Applications "$STAGE/Applications"
hdiutil create -quiet -volname "NFS Hot Pursuit 2" -srcfolder "$STAGE" -fs HFS+ -format UDZO "$DMG"
(cd "$OUT" && shasum -a 256 "$(basename "$DMG")" > "$(basename "$DMG").sha256")
tests/dmg_no_game_code.sh "$DMG"
echo "dmg: $DMG ($(du -h "$DMG" | cut -f1))"
