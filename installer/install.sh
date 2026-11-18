#!/bin/bash
# NFS Hot Pursuit 2 Installer: the bundle's executable. The game is built on the player's Mac from their own disc image:
#   1. Apple's Command Line Tools (clt.sh)
#   2. the disc image: NFSHP2/ and SLUS_203.62 (SHA-1 checked) into ~/Library/Application Support/NFS Hot Pursuit 2/game
#   3. the game's program translated to C++ (upstream's gen.py, gen_iop.py) and compiled with clang (build_game.sh)
#   4. "NFS Hot Pursuit 2.app" in /Applications (~/Applications when /Applications isn't writable), then opened
# The installer holds no game code or data. Environment: HP2_IMPORT_ISO=<iso> (no file dialog), HP2_INSTALL_DIR=<folder> (where the
# app goes), HP2_NO_UI=1 (no dialogs, no progress window, no opening: tests), HP2_BUILD_JOBS=<n>.
set -uo pipefail
RES="$(cd "$(dirname "$0")/../Resources" && pwd)"
KIT="$RES/kit"
VERSION="$(cat "$KIT/VERSION")"
USER_DIR="$HOME/Library/Application Support/NFS Hot Pursuit 2"
GAME="$USER_DIR/game"
WORK="$USER_DIR/build"
APP_NAME="NFS Hot Pursuit 2.app"
SHA1=6397d4425d55056678ed5a405cc81542f7d4d2ee
mkdir -p "$USER_DIR/logs" "$WORK"
LOG="$USER_DIR/logs/install.log"
PROG="$WORK/progress.txt"
exec >"$LOG" 2>&1
echo "installer $VERSION, $(sw_vers -productVersion), $(date)"
: > "$PROG"

ui() { [ -z "${HP2_NO_UI:-}" ]; }
alert() { ui && /usr/bin/osascript -e "display alert \"NFS Hot Pursuit 2\" message \"$1\" as critical" >/dev/null 2>&1; echo "alert: $1"; }
fail() { echo "100 ERROR: $1" >> "$PROG"; alert "$1

The log: ~/Library/Application Support/NFS Hot Pursuit 2/logs/install.log"; exit 1; }
say() { echo "$1 $2" >> "$PROG"; echo "== $2"; }

# where the app goes
if [ -n "${HP2_INSTALL_DIR:-}" ]; then DEST="$HP2_INSTALL_DIR"
elif [ -w /Applications ]; then DEST=/Applications
else DEST="$HOME/Applications"; fi
mkdir -p "$DEST"

# built already: open it, or build it again
if [ -d "$DEST/$APP_NAME" ] && ui; then
  choice=$(/usr/bin/osascript -e "button returned of (display dialog \"NFS Hot Pursuit 2 is installed in $DEST.\" buttons {\"Rebuild\", \"Quit\", \"Play\"} default button \"Play\" with title \"NFS Hot Pursuit 2\")" 2>/dev/null)
  case "$choice" in
    Play) open "$DEST/$APP_NAME"; exit 0 ;;
    Rebuild) ;;
    *) exit 0 ;;
  esac
fi

# 1. Command Line Tools
bash "$KIT/clt.sh" "$PROG" || fail "Apple's Command Line Tools are needed to build the game. In Terminal: xcode-select --install"

# 2. the disc
installed() { [ -f "$GAME/.installed" ] && [ -f "$GAME/NFSHP2/ZZDATA.BIN" ] && [ "$(shasum -a 1 "$GAME/data/SLUS_203.62" 2>/dev/null | cut -d' ' -f1)" = $SHA1 ]; }
if ! installed; then
  ISO="${HP2_IMPORT_ISO:-}"
  if [ -z "$ISO" ]; then
    ui || fail "no disc image (HP2_IMPORT_ISO)"
    ISO=$(/usr/bin/osascript -e 'POSIX path of (choose file with prompt "Choose your disc image of Need for Speed: Hot Pursuit 2 (PlayStation 2, USA, SLUS-20362):" of type {"iso", "public.iso-image"})' 2>/dev/null) || exit 0
  fi
  [ -f "$ISO" ] || fail "No such disc image: $ISO"
  ui && { /usr/bin/osascript -l JavaScript "$KIT/progress.js" "$PROG" >/dev/null 2>&1 & }
  say 2 "Copying the game's files from the disc image (2.9 GB)"
  rm -rf "$GAME.part"; mkdir -p "$GAME.part/data"
  /usr/bin/bsdtar -xf "$ISO" -C "$GAME.part" NFSHP2 SLUS_203.62 || fail "That file is not a disc image of Hot Pursuit 2 for the PlayStation 2."
  [ -d "$GAME.part/NFSHP2" ] && [ -f "$GAME.part/SLUS_203.62" ] || fail "That disc image has no NFSHP2 folder or SLUS_203.62: is it Hot Pursuit 2 for the PlayStation 2?"
  mv "$GAME.part/SLUS_203.62" "$GAME.part/data/SLUS_203.62"
  [ "$(shasum -a 1 "$GAME.part/data/SLUS_203.62" | cut -d' ' -f1)" = $SHA1 ] \
    || fail "This disc's program is not the version the port is made for. Supported: USA SLUS-20362 v1.00 (Europe v1.00 has the same program)."
  cp -R "$KIT/src/upstream/recomp/data/." "$GAME.part/data/"
  chmod -R u+w "$GAME.part"
  echo "$ISO" > "$GAME.part/.installed"
  rm -rf "$GAME" && mv "$GAME.part" "$GAME"
else
  ui && { /usr/bin/osascript -l JavaScript "$KIT/progress.js" "$PROG" >/dev/null 2>&1 & }
fi

# 3. the game's code as C++, then compiled (a copy of the kit's sources: the generator writes beside itself)
say 5 "Translating the game's program to C++"
rm -rf "$WORK/src" && cp -R "$KIT/src" "$WORK/src"
/usr/bin/python3 -I "$WORK/src/upstream/tools/recomp/gen.py" --elf "$GAME/data/SLUS_203.62" || fail "Translating the game's program failed."
/usr/bin/python3 -I "$WORK/src/upstream/tools/recomp/gen_iop.py" --irx "$GAME/NFSHP2/IRX253/STREAM.IRX" || fail "Translating the sound driver failed."
bash "$KIT/src/mac/build_game.sh" "$WORK/src" "$WORK/src/upstream/work/recomp/gen" "$KIT/shaders" "$KIT/deps" "$WORK/obj" "$WORK/hp2" "$PROG" \
  || fail "Building the game failed."

# 4. the app
say 97 "Putting NFS Hot Pursuit 2 in $DEST"
bash "$KIT/src/mac/assemble_app.sh" "$WORK/hp2" "$KIT/deps" "$KIT/src/upstream/recomp/data" "$KIT/hp2.icns" "$DEST/$APP_NAME" "$VERSION" \
  || fail "Putting the app in $DEST failed."
rm -rf "$WORK/src" "$WORK/obj" "$WORK/hp2"      # the build's copy of the game's code goes; the game's files stay
say 100 "Done: NFS Hot Pursuit 2 is in $DEST"
echo "installed: $DEST/$APP_NAME"
ui && { sleep 1; open "$DEST/$APP_NAME"; }
exit 0
