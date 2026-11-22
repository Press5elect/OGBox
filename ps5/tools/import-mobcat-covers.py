#!/usr/bin/env python3
# OGBox - the index of MobCat's box pictures (github.com/MobCat/MobCats-original-xbox-game-list) as
# ps5/gamedata/covers-mobcat.txt. OGBox downloads the picture a game needs at run time
# (ogbox_library.cpp); no picture is in the repository or the title.
# Copyright (C) 2026 Press5elect. SPDX-License-Identifier: GPL-3.0-or-later
#
# 1.0.3: made from MobCat's database (titleIDs.db, mobcat.zip/XboxIDs) and the repository's file
# list: each release (its title ID and version, as its default.xbe has them) with the names of its
# box's pictures, in the order the database gives them (Cover_Stats: the first is <XMID>.jpg, the
# others <XMID>-<label>.jpg), each with where it is: cover/<first 2>/<name>.jpg (the whole box's
# scan, 1 to 2 MB) and thumbnail/<first 2>/<name>.jpg (the front alone, 300 x 427). A picture the
# repository has under a release's XMID that the database doesn't list comes after its own.
# Only identifiers are taken from the database (title IDs, versions, XMIDs, the pictures' labels):
# none of its names or other text. The format (the console reads it with fgets, one line each):
#   <title ID, 8 hex> <version, hex> <region: the XMID's last letter> <XMID> <picture> [<picture> ...]
#     a picture: where it is ("c" the scan, "t" the thumbnail, both "ct"), then "-<label>" for
#     <XMID>-<label>.jpg, or for <XMID>.jpg "=<label>" (the box it is, when the database says)
#     ("ct=NTSC-U", "t-PAL-DEU")
#   i <title ID>    MobCat has its title icon, icon/<first 4>/<title ID>.png
# A release none of whose pictures is in the repository is left out (the console's guess from the
# serial then looks at the other releases'). Lines in order of title ID, version, XMID.
#
#   python3 -I ps5/tools/import-mobcat-covers.py [titleIDs.db] [a clone of the repository]
# Without a database it is downloaded; without a clone, a blob-less one is made (only the file list
# is read). The database is someone else's data: it is opened read-only and its fields are checked.
import hashlib
import json
import os
import re
import sqlite3
import subprocess
import sys
import tempfile
import urllib.request

REPO = "https://github.com/MobCat/MobCats-original-xbox-game-list"
DB_URL = "https://mobcat.zip/XboxIDs/titleIDs.db"
MAX_LINE = 1000   # (ogbox_library.cpp reads lines of up to 1023 bytes)


def stems(names, top):
    out = {}
    for n in names:
        parts = n.split("/")
        if len(parts) == 3 and parts[0] == top and n.lower().endswith(".jpg"):
            out[os.path.splitext(parts[2])[0]] = n
    return out


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "gamedata", "covers-mobcat.txt")
    with tempfile.TemporaryDirectory() as tmp:
        db = sys.argv[1] if len(sys.argv) > 1 else os.path.join(tmp, "titleIDs.db")
        if len(sys.argv) <= 1:
            with urllib.request.urlopen(DB_URL, timeout=120) as r, open(db, "wb") as f:
                f.write(r.read())
        clone = sys.argv[2] if len(sys.argv) > 2 else os.path.join(tmp, "mobcat")
        if len(sys.argv) <= 2:
            subprocess.run(["git", "clone", "-q", "--depth", "1", "--filter=blob:none", "--no-checkout", REPO, clone],
                           check=True)
        commit = subprocess.run(["git", "-C", clone, "rev-parse", "HEAD"], check=True, capture_output=True,
                                text=True).stdout.strip()
        names = subprocess.run(["git", "-C", clone, "ls-tree", "-r", "--name-only", "HEAD"], check=True,
                               capture_output=True, text=True).stdout.split("\n")
        with open(db, "rb") as f:
            db_sha = hashlib.sha256(f.read()).hexdigest()
        con = sqlite3.connect("file:%s?mode=ro" % db, uri=True)
        rows = con.execute("SELECT Title_ID, Version, XMID, Cover_Stats FROM TitleIDs").fetchall()
        con.close()

    scans, thumbs = stems(names, "cover"), stems(names, "thumbnail")
    icons = set()
    for n in names:
        parts = n.split("/")
        if len(parts) == 3 and parts[0] == "icon" and re.fullmatch(r"[0-9A-F]{8}\.png", parts[2]) and \
                parts[1] == parts[2][:4]:
            icons.add(parts[2][:8])
    by_xmid = {}
    for s in set(scans) | set(thumbs):
        by_xmid.setdefault(s[:8], []).append(s)

    releases, plain_label, skipped = {}, {}, 0
    for tid, ver, xmid, stats in rows:
        tid, ver, xmid = (tid or "").upper(), (ver or ""), (xmid or "").upper()
        if not re.fullmatch(r"[0-9A-F]{8}", tid) or not re.fullmatch(r"0x[0-9A-Fa-f]{1,8}", ver) or \
                not re.fullmatch(r"[A-Z0-9]{7}[AJKELW]", xmid):
            skipped += 1
            continue
        try:
            labels = json.loads(stats) if stats else []
        except ValueError:
            labels = []
        if not isinstance(labels, list):
            labels = []
        key = (tid, int(ver, 16), xmid)
        listed = releases.setdefault(key, [])
        for i, label in enumerate(labels):
            if not isinstance(label, str) or not re.fullmatch(r"[A-Za-z0-9-]{1,48}", label):
                continue
            name = xmid if i == 0 else xmid + "-" + label
            if i == 0:
                plain_label.setdefault(xmid, label)
            if name not in listed:
                listed.append(name)
    lines, pictures = [], 0
    for (tid, ver, xmid) in sorted(releases):
        listed = releases[(tid, ver, xmid)]
        listed += sorted(n for n in by_xmid.get(xmid, []) if n not in listed)
        pics = []
        for name in listed:
            where = ("c" if name in scans else "") + ("t" if name in thumbs else "")
            if where:
                pics.append(where + (name[8:] if len(name) > 8 else
                                     "=" + plain_label[xmid] if xmid in plain_label else ""))
        if not pics:
            continue
        pictures += len(pics)
        line = "%s %X %s %s %s" % (tid, ver, xmid[-1], xmid, " ".join(pics))
        assert len(line) < MAX_LINE, line
        lines.append(line)
    with open(out, "w") as f:
        f.write("# OGBox: MobCat's box pictures (%s, commit %s; its database %s, sha256 %s)\n" %
                (REPO, commit, DB_URL, db_sha))
        f.write("# <title ID> <version, hex> <region letter> <XMID> <picture>...; a picture: c (cover/, the scan) "
                "and/or t (thumbnail/, the front), then -<label> for <XMID>-<label>.jpg, =<label> for <XMID>.jpg. "
                "i <title ID>: an icon. "
                "Made by ps5/tools/import-mobcat-covers.py\n")
        for line in lines:
            f.write(line + "\n")
        for tid in sorted(icons):
            f.write("i %s\n" % tid)
    print("%d releases (%d rows, %d skipped), %d pictures, %d icons; commit %s -> %s" %
          (len(lines), len(rows), skipped, pictures, len(icons), commit[:12], out))


if __name__ == "__main__":
    main()
