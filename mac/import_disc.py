#!/usr/bin/env python3
"""The installer's disc import: the player's own disc image into the game folder.

    python3 -I import_disc.py <disc image> <game dir> <port data dir>

Extracts NFSHP2/ and SLUS_203.62 from the ISO with macOS's bsdtar into <game dir>.part, checks the program's SHA-1, adds the port's
data files (engine/recomp/data) to data/ beside the program, then renames it to <game dir>. The port is generated from the USA
program (SLUS-20362 v1.00; Europe v1.00 has the same one); its hooks are at that program's addresses, so another cannot run.
Exit 0 = done; 3 = not a PS2 disc image; 4 = a PS2 disc, not Hot Pursuit 2; 5 = Hot Pursuit 2, another edition.
Same interface as the NFS Underground ports' mac/import_disc.py.
"""
import hashlib
import os
import shutil
import stat
import subprocess
import sys

PROGRAM = "SLUS_203.62"
PROGRAM_SHA1 = "6397d4425d55056678ed5a405cc81542f7d4d2ee"   # Need for Speed: Hot Pursuit 2 (USA) v1.00


def say(text):
    print("import: " + text, flush=True)


def listing(iso):
    r = subprocess.run(["/usr/bin/bsdtar", "-tf", iso], capture_output=True, text=True)
    return r.returncode, [n.rstrip("/") for n in r.stdout.splitlines()]


def main():
    iso, dest, port_data = sys.argv[1], sys.argv[2], sys.argv[3]
    tmp = dest + ".part"
    say("%s -> %s" % (iso, dest))
    code, names = listing(iso)
    if code != 0 or "SYSTEM.CNF" not in names:
        say("not a PS2 disc image")
        return 3
    if PROGRAM not in names or "NFSHP2" not in names:
        say("a PS2 disc, not Hot Pursuit 2 (no %s or NFSHP2/)" % PROGRAM)
        return 4
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(os.path.join(tmp, "data"))
    if subprocess.run(["/usr/bin/bsdtar", "-xf", iso, "-C", tmp, "NFSHP2", PROGRAM]).returncode != 0:
        shutil.rmtree(tmp, ignore_errors=True)
        say("reading the disc image failed")
        return 3
    program = os.path.join(tmp, "data", PROGRAM)
    os.rename(os.path.join(tmp, PROGRAM), program)
    with open(program, "rb") as f:
        sha = hashlib.sha1(f.read()).hexdigest()
    if sha != PROGRAM_SHA1:
        shutil.rmtree(tmp, ignore_errors=True)
        say("%s %s: Hot Pursuit 2, another edition" % (PROGRAM, sha))
        return 5
    shutil.copytree(port_data, os.path.join(tmp, "data"), dirs_exist_ok=True)
    for root, dirs, files in os.walk(tmp):      # the disc's files come out read-only
        for n in dirs + files:
            p = os.path.join(root, n)
            os.chmod(p, os.stat(p).st_mode | stat.S_IWUSR)
    with open(os.path.join(tmp, ".installed"), "w") as f:
        f.write(iso + "\n")
    shutil.rmtree(dest, ignore_errors=True)
    os.rename(tmp, dest)
    say("done, %s %s" % (PROGRAM, sha))
    return 0


if __name__ == "__main__":
    sys.exit(main())
