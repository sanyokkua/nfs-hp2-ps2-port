#!/bin/bash
# The players' DMG: "NFS Hot Pursuit 2 Installer.app" with its kit (our sources, upstream nfshp2-port-win's sources at the pin with
# patches/, compiled shaders, SDL3 + MoltenVK + headers for macOS 15+). No game code and no game data: the installer builds the
# game on the player's Mac from their own disc. Checked by tests/dmg_no_game_code.sh before it is called done.
#   tools/make_dmg.sh            -> build/dmg/NFS-Hot-Pursuit-2-<version>.dmg (+ .sha256)
# Needs: tools/fetch.sh (upstream/ on branch mac), tools/build_deps.sh (build/deps), cmake configured in build/mac (shaders).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
VERSION="$(cat VERSION)"
OUT=build/dmg; STAGE="$OUT/stage"; APP="$STAGE/NFS Hot Pursuit 2 Installer.app"; KIT="$APP/Contents/Resources/kit"
DMG="$OUT/NFS-Hot-Pursuit-2-$VERSION.dmg"
[ "$(git -C upstream rev-parse --abbrev-ref HEAD)" = mac ] || { echo "upstream/ is not on branch mac: run tools/fetch.sh"; exit 1; }
[ -z "$(git -C upstream status --porcelain -- recomp tools)" ] || { echo "upstream/ has changes not in patches/: commit them on mac and export"; exit 1; }
[ -f build/deps/lib/libSDL3.0.dylib ] || { echo "run tools/build_deps.sh first"; exit 1; }
cmake --build build/mac --target hp2_shaders >/dev/null

rm -rf "$OUT" && mkdir -p "$APP/Contents/MacOS" "$KIT/src/upstream/recomp" "$KIT/deps" "$KIT/shaders" "$KIT/licenses"
# the installer
cp installer/install.sh "$APP/Contents/MacOS/installer"
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
# the sources: ours, and upstream's (the runtime, the generator and its function table, the data files, its docs) at the pin
rsync -a --exclude headless_stubs.cpp --exclude __pycache__ mac/ "$KIT/src/mac/"
rsync -a --exclude __pycache__ upstream/tools "$KIT/src/upstream/"
rsync -a upstream/recomp/runtime upstream/recomp/data upstream/recomp/symbols upstream/recomp/docs "$KIT/src/upstream/recomp/"
cp upstream/README.md upstream/TECHNICAL.md "$KIT/src/upstream/"
PIN="$(sed -n 's/^PIN=\([0-9a-f]*\).*/\1/p' tools/fetch.sh)"
echo "nfshp2-port-win by ZloiKILLER, https://github.com/ZloiKILLER/nfshp2-port-win, commit $PIN, with this project's patches/ applied" > "$KIT/src/upstream/SOURCE.txt"
cp build/mac/shaders/*.spv.h "$KIT/shaders/"
rsync -a build/deps/include build/deps/lib "$KIT/deps/"
# licenses and notices
cp -R LICENSES/. "$KIT/licenses/"
cp -R build/deps/share/licenses/. "$KIT/licenses/"
cp LICENSE THIRD_PARTY_NOTICES.md "$KIT/"
codesign --force --sign - "$APP" >/dev/null 2>&1
# the DMG's other files
cp docs/dmg/read-me-first.txt "$STAGE/Read me first.txt"
cp THIRD_PARTY_NOTICES.md "$STAGE/Third-party notices.md"
ln -s /Applications "$STAGE/Applications"
hdiutil create -quiet -volname "NFS Hot Pursuit 2" -srcfolder "$STAGE" -fs HFS+ -format UDZO "$DMG"
(cd "$OUT" && shasum -a 256 "$(basename "$DMG")" > "$(basename "$DMG").sha256")
tests/dmg_no_game_code.sh "$DMG"
echo "dmg: $DMG ($(du -h "$DMG" | cut -f1))"
