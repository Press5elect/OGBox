#!/usr/bin/env python3
# OGBox - the index of the Xbox scene's trainers xbased keeps (github.com/crazyriddler/xbased,
# cheats/<title ID>/<file>) as ps5/gamedata/cheats-xbased.txt. Settings > Updates > "Download cheats"
# fetches the files it lists into the user's trainers folder when the user asks (ogbox_fixes_fetch.h);
# no trainer is in the repository or the title.
# SPDX-FileCopyrightText: 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
#
# 1.0.3: read from xbased's commit COMMIT (its git objects, so the hashes are those of the files at
# raw.githubusercontent.com/crazyriddler/xbased/<COMMIT>/cheats/...). Only the file list, the sizes
# and the SHA-256s are taken: none of the files' contents. Left out: the files xbased's
# cheats/removed.txt names (it found they patch without checking the build they were made for),
# anything not .etm or .xbtf, and a name the console would refuse (SafeCheatPath in
# ogbox_fixes_fetch.cpp: printable ASCII only; none of / \ " * : < > ? |; no ".."; not starting with
# '.' or ' ', not ending with '.' or ' '; at most 200 bytes), each said on stderr. The format:
#   # OGBox cheats 1 crazyriddler/xbased <commit>
#   <sha256> <bytes> cheats/<title ID, 8 hex>/<file>     (the name may have spaces: the rest of the line)
#
#   python3 -I ps5/tools/import-xbased-cheats.py [a clone of xbased]
# Without a clone a blob-less one of COMMIT is made and its cheats/ fetched. The clone is someone
# else's data: only git's plumbing reads it (ls-tree, cat-file), nothing in it is run.
import hashlib
import os
import re
import subprocess
import sys
import tempfile

REPO = "https://github.com/crazyriddler/xbased"
COMMIT = "2031611e0aebed3c3852af16a63108cabd9554a0"
MOST_BYTES = 8 << 20          # (ogbox_fixes_fetch.cpp's kMostBytes)
TITLE = re.compile(r"^[0-9A-F]{8}$")
BAD = set('/\\"*:<>?|')


def git(clone, *args, **kw):
    return subprocess.run(["git", "-c", "core.fsmonitor=false", "-c", "core.hooksPath=/dev/null", "-C", clone] +
                          list(args), check=True, capture_output=True, **kw).stdout


def refused(name):
    """Why the console would refuse the name (SafeCheatPath), or None."""
    if not name or len(name.encode()) > 200:
        return "empty or longer than 200 bytes"
    if any(ord(c) < 0x20 or ord(c) > 0x7e for c in name):
        return "a control or non-ASCII character"
    if any(c in BAD for c in name):
        return "a character the file systems can't take"
    if ".." in name:
        return '".." in it'
    if name[0] in ". " or name[-1] in ". ":
        return "starts with '.' or ' ', or ends with '.' or ' '"
    if os.path.splitext(name)[1].lower() not in (".etm", ".xbtf"):
        return "not .etm or .xbtf"
    return None


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "gamedata", "cheats-xbased.txt")
    with tempfile.TemporaryDirectory() as tmp:
        clone = sys.argv[1] if len(sys.argv) > 1 else os.path.join(tmp, "xbased")
        if len(sys.argv) <= 1:
            subprocess.run(["git", "init", "-q", clone], check=True)
            git(clone, "remote", "add", "origin", REPO)
            git(clone, "fetch", "-q", "--depth", "1", "--filter=blob:none", "origin", COMMIT)
            git(clone, "sparse-checkout", "set", "cheats")
            git(clone, "-c", "advice.detachedHead=false", "checkout", "-q", COMMIT)   # (fetches cheats/' blobs)
        entries = git(clone, "ls-tree", "-r", "-z", "--full-tree", COMMIT, "cheats/").split(b"\0")
        blobs = []
        for e in entries:
            if not e:
                continue
            meta, path = e.split(b"\t", 1)
            mode, kind, oid = meta.split(b" ")
            blobs.append((path.decode("utf-8", "replace"), kind.decode(), mode.decode(), oid.decode()))
        removed = set()
        for oid_path in blobs:
            if oid_path[0] == "cheats/removed.txt":
                text = git(clone, "cat-file", "blob", oid_path[3]).decode("utf-8", "replace")
                for line in text.splitlines():
                    if line.startswith("#") or not line.strip():
                        continue
                    # <title id>/<file> <size> <FNV-1a 64>: the name may have spaces
                    m = re.match(r"^(.+) (\d+) ([0-9a-f]{16})$", line)
                    if m:
                        removed.add("cheats/" + m.group(1))
        lines, skipped, seen = [], [], set()
        for path, kind, mode, oid in sorted(blobs):
            parts = path.split("/")
            if path == "cheats/removed.txt":
                continue
            if path in removed:
                skipped.append((path, "in xbased's removed.txt"))
                continue
            if kind != "blob" or mode not in ("100644", "100755") or len(parts) != 3 or not TITLE.match(parts[1]):
                skipped.append((path, "not cheats/<title ID>/<file>"))
                continue
            why = refused(parts[2])
            if why:
                skipped.append((path, why))
                continue
            if path.lower() in seen:   # (exFAT and the console's file systems don't tell case apart)
                skipped.append((path, "another file's name but for case"))
                continue
            data = git(clone, "cat-file", "blob", oid)
            if not data or len(data) > MOST_BYTES:
                skipped.append((path, "empty or larger than 8 MiB"))
                continue
            seen.add(path.lower())
            lines.append("%s %d %s" % (hashlib.sha256(data).hexdigest(), len(data), path))
    head = ["# OGBox cheats 1 crazyriddler/xbased " + COMMIT,
            "# The Xbox scene's trainers in xbased's repository (" + REPO + ", commit " + COMMIT + "), fetched only "
            "when the user asks (Settings > Updates > Download cheats) from raw.githubusercontent.com/crazyriddler/"
            "xbased/<commit>/<path>. Not part of OGBox; this index holds only paths, sizes and hashes.",
            "# <sha256> <bytes> <path>: made by ps5/tools/import-xbased-cheats.py; " + str(len(skipped)) +
            " files left out (removed.txt's, or names the console refuses)"]
    with open(out, "w", newline="\n") as f:
        f.write("\n".join(head + lines) + "\n")
    for path, why in skipped:
        print("import-xbased-cheats: left out %s: %s" % (path, why), file=sys.stderr)
    print("import-xbased-cheats: %s: %d files, %d left out" % (os.path.normpath(out), len(lines), len(skipped)))


if __name__ == "__main__":
    main()
