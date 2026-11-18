#!/bin/bash
# The game's .app from a built binary: MoltenVK and SDL3 in Frameworks, the port's data files and the icon in Resources, ad-hoc
# signed. Run by the installer on the player's Mac and by tools/make_app.sh.
#   mac/assemble_app.sh <binary> <deps (lib/)> <port data dir> <icon .icns> <app path> <version>
set -euo pipefail
BIN="$1" DEPS="$2" DATA="$3" ICON="$4" APP="$5" VERSION="$6"
rm -rf "$APP.part"
mkdir -p "$APP.part/Contents/MacOS" "$APP.part/Contents/Frameworks" "$APP.part/Contents/Resources"
cp "$BIN" "$APP.part/Contents/MacOS/hp2"
cp "$DEPS/lib/libSDL3.0.dylib" "$DEPS/lib/libMoltenVK.dylib" "$APP.part/Contents/Frameworks/"
cp -R "$DATA" "$APP.part/Contents/Resources/data"
cp "$ICON" "$APP.part/Contents/Resources/hp2.icns"
cat > "$APP.part/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>NFS Hot Pursuit 2</string>
  <key>CFBundleDisplayName</key><string>NFS Hot Pursuit 2</string>
  <key>CFBundleIdentifier</key><string>io.github.sanyokkua.nfshp2</string>
  <key>CFBundleExecutable</key><string>hp2</string>
  <key>CFBundleIconFile</key><string>hp2</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>LSMinimumSystemVersion</key><string>15.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.racing-games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>GCSupportsControllerUserInteraction</key><true/>
</dict></plist>
PLIST
codesign --force --sign - "$APP.part/Contents/Frameworks/"*.dylib >/dev/null 2>&1
codesign --force --sign - "$APP.part" >/dev/null 2>&1
rm -rf "$APP" && mv "$APP.part" "$APP"
