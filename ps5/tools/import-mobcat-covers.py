#!/usr/bin/env python3
# OGBox - the index of MobCat's cover scans (github.com/MobCat/MobCats-original-xbox-game-list,
# cover/<2 letters>/<XMID>[-<variant>].jpg) as ps5/gamedata/covers-mobcat.txt: one file name stem
# a line, the commit first. OGBox downloads the scan a game needs at run time (ogbox_library.cpp);
# no picture is in the repository or the title.
# Copyright (C) 2026 Press5elect. SPDX-License-Identifier: GPL-3.0-or-later
#
#   python3 ps5/tools/import-mobcat-covers.py [a clone of the repository]
import os
import subprocess
import sys
import tempfile

REPO = "https://github.com/MobCat/MobCats-original-xbox-game-list"


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "gamedata", "covers-mobcat.txt")
    with tempfile.TemporaryDirectory() as tmp:
        clone = sys.argv[1] if len(sys.argv) > 1 else os.path.join(tmp, "mobcat")
        if len(sys.argv) <= 1:
            subprocess.run(["git", "clone", "-q", "--depth", "1", "--filter=blob:none", "--no-checkout", REPO, clone],
                           check=True)
        commit = subprocess.run(["git", "-C", clone, "rev-parse", "HEAD"], check=True, capture_output=True,
                                text=True).stdout.strip()
        names = subprocess.run(["git", "-C", clone, "ls-tree", "-r", "--name-only", "HEAD", "cover/"], check=True,
                               capture_output=True, text=True).stdout.split()
    stems = sorted({os.path.splitext(os.path.basename(n))[0] for n in names if n.lower().endswith(".jpg")})
    with open(out, "w") as f:
        f.write("# OGBox: MobCat's cover scans (%s, commit %s): cover/<first 2>/<stem>.jpg\n" % (REPO, commit))
        f.write("# <serial without its dash><version, 2 digits><region: A J K E L W>[-<variant>]\n")
        for s in stems:
            f.write(s + "\n")
    print("%d covers, commit %s -> %s" % (len(stems), commit[:12], out))


if __name__ == "__main__":
    main()
