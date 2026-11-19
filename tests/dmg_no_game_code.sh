#!/bin/bash
# The players' DMG holds no game code and no game data. Fails on:
#   - the disc's files by name (SLUS_*, ZZDATA.BIN, ZDIR.BIN, *.IRX, *.IMG, SYSTEM.CNF) or *.iso
#   - the generated C++ or objects made from it (code_*.cpp, functable.cpp, funcs.h, iop_stream_*.cpp, *.o) or a game binary (hp2)
#   - any file with the SHA-1 of a file on the disc (game/extracted, when it is there)
#   - two or more of 64 64-byte windows of the program's .text in one file
#   - this Mac's home folder path (the builder's)
# Adapted from NFSU2Deadware's tests/dmg_no_game_code.sh (MIT).   tests/dmg_no_game_code.sh <dmg>
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DMG="$1"; MNT="$(mktemp -d "${TMPDIR:-/tmp}/hp2dmg.XXXXXX")"
hdiutil attach -quiet -nobrowse -readonly -mountpoint "$MNT" "$DMG"
trap 'hdiutil detach -quiet "$MNT"; rmdir "$MNT"' EXIT
fail=0
bad() { echo "FAIL: $1"; fail=1; }
while IFS= read -r f; do bad "a game file by name: ${f#$MNT/}"; done < <(find "$MNT" \( -iname 'SLUS_*' -o -iname ZZDATA.BIN -o -iname ZDIR.BIN \
  -o -iname '*.IRX' -o -iname '*.IMG' -o -iname SYSTEM.CNF -o -iname '*.iso' -o -name 'code_*.cpp' -o -name functable.cpp -o -name funcs.h \
  -o -name 'iop_stream_*.cpp' -o -name '*.o' -o -name hp2 \) -not -path '*/Applications/*')
GAME="$ROOT/game/extracted"
python3 -I - "$MNT" "$GAME" <<'PY' || fail=1
import hashlib, os, struct, sys
mnt, game = sys.argv[1], sys.argv[2]
def files(top):
    for d, dirs, names in os.walk(top):
        if d.startswith(os.path.join(mnt, 'Applications')): continue
        for n in names:
            p = os.path.join(d, n)
            if os.path.isfile(p) and not os.path.islink(p): yield p
bad = 0
if os.path.isdir(game):
    disc = set()
    disc_files = list(files(os.path.join(game, 'NFSHP2'))) + [os.path.join(game, 'data', 'SLUS_203.62')]   # the disc's own (data/ has upstream's too)
    for p in disc_files:
        if os.path.getsize(p) < 512 * 1024 * 1024:
            disc.add(hashlib.sha1(open(p, 'rb').read()).hexdigest())
    elf = open(os.path.join(game, 'data', 'SLUS_203.62'), 'rb').read()
    # .text: the ELF's section headers
    shoff, = struct.unpack_from('<I', elf, 32); shnum, shstrndx = struct.unpack_from('<HH', elf, 48)
    sec = [struct.unpack_from('<IIIIII', elf, shoff + i * 40) for i in range(shnum)]
    names = sec[shstrndx][4]
    text = next(s for s in sec if elf[names + s[0]:].split(b'\0')[0] == b'.text')
    t = elf[text[4]:text[4] + text[5]]
    windows = [t[i:i + 64] for i in range(0, len(t) - 64, len(t) // 64)][:64]
    for p in files(mnt):
        data = open(p, 'rb').read()
        if hashlib.sha1(data).hexdigest() in disc:
            print('FAIL: a file of the disc:', p[len(mnt):]); bad = 1
        hits = sum(1 for w in windows if w in data)
        if hits >= 2:
            print('FAIL: %d windows of the program\'s code in %s' % (hits, p[len(mnt):])); bad = 1
else:
    print('note: no game/extracted here: the SHA-1 and code-window checks were skipped')
home = os.path.expanduser('~').encode()
for p in files(mnt):
    if home in open(p, 'rb').read():
        print('FAIL: this Mac\'s home path in', p[len(mnt):]); bad = 1
sys.exit(bad)
PY
[ $fail -eq 0 ] && echo "dmg_no_game_code: PASS ($DMG)" || { echo "dmg_no_game_code: FAILED"; exit 1; }
