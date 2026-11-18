#!/bin/bash
# build/<dir>/hp2 -> build/app/SpeedBreaker HP2.app: the game, MoltenVK and SDL3 in Frameworks, the port's data files in Resources,
# ad-hoc signed. The app holds the game's code compiled from your disc: it is for your own Mac, not to be shared.
#   tools/make_app.sh [build dir, default build/mac] [out dir, default build/app]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
BUILD="${1:-build/mac}"; OUT="${2:-build/app}"
APP="$OUT/SpeedBreaker HP2.app"
VERSION=$(git describe --tags --always 2>/dev/null || echo 0.1.0)
rm -rf "$APP"; mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
cp "$BUILD/hp2" "$APP/Contents/MacOS/hp2"
cp -R upstream/recomp/data "$APP/Contents/Resources/data"
# the libraries: the real files (not Homebrew's links), named as the binary asks for them
SDL=$(otool -L "$BUILD/hp2" | awk '/libSDL3/ {print $1}')
cp -L "$SDL" "$APP/Contents/Frameworks/$(basename "$SDL")"
install_name_tool -change "$SDL" "@executable_path/../Frameworks/$(basename "$SDL")" "$APP/Contents/MacOS/hp2"
install_name_tool -id "@executable_path/../Frameworks/$(basename "$SDL")" "$APP/Contents/Frameworks/$(basename "$SDL")"
cp -L "$(brew --prefix molten-vk)/lib/libMoltenVK.dylib" "$APP/Contents/Frameworks/libMoltenVK.dylib"
install_name_tool -id "@executable_path/../Frameworks/libMoltenVK.dylib" "$APP/Contents/Frameworks/libMoltenVK.dylib"
# the icon: upstream's speed.ico, largest image
ICONSET="$OUT/hp2.iconset"; rm -rf "$ICONSET"; mkdir -p "$ICONSET"
sips -s format png upstream/recomp/runtime/speed.ico --out "$OUT/icon.png" >/dev/null
for s in 16 32 128 256 512; do
  sips -z $s $s "$OUT/icon.png" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
  sips -z $((s*2)) $((s*2)) "$OUT/icon.png" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/hp2.icns"; rm -rf "$ICONSET" "$OUT/icon.png"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>SpeedBreaker HP2</string>
  <key>CFBundleDisplayName</key><string>SpeedBreaker HP2</string>
  <key>CFBundleIdentifier</key><string>local.speedbreaker.hp2</string>
  <key>CFBundleExecutable</key><string>hp2</string>
  <key>CFBundleIconFile</key><string>hp2</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>LSMinimumSystemVersion</key><string>$(sw_vers -productVersion | cut -d. -f1).0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.racing-games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>GCSupportsControllerUserInteraction</key><true/>
</dict></plist>
PLIST
codesign --force --sign - "$APP/Contents/Frameworks/"*.dylib
codesign --force --sign - "$APP"
echo "app: $APP ($(du -sh "$APP" | cut -f1))"
