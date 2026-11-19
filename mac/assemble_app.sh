#!/bin/bash
# The game's .app from a built binary: MoltenVK and SDL3 in Frameworks, the icon in Resources, ad-hoc signed. Run by the installer
# on the player's Mac and by tools/make_app.sh. (The port's data files are in the game folder, beside the disc's: mac/import_disc.py.)
#   mac/assemble_app.sh <binary> <libs dir> <icon .icns> <app path> <version>
set -euo pipefail
BIN="$1" LIBS="$2" ICON="$3" APP="$4" VERSION="$5"
rm -rf "$APP.part"
mkdir -p "$APP.part/Contents/MacOS" "$APP.part/Contents/Frameworks" "$APP.part/Contents/Resources"
cp "$BIN" "$APP.part/Contents/MacOS/hp2"
cp "$LIBS/libSDL3.0.dylib" "$LIBS/libMoltenVK.dylib" "$APP.part/Contents/Frameworks/"
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
