#!/usr/bin/env python3
# OGBox - the list of game fixes OGBox fetches from GitHub at start (src/ogbox_fixes_fetch.h).
# SPDX-FileCopyrightText: 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
#
#   ps5/tools/game-fixes-index.py          # writes ps5/game-fixes.txt
#   ps5/tools/game-fixes-index.py --check  # fails when it isn't current (the PC tests run this)
#
# Every file of ps5/mods (not README.md) and gamedata's XBE patches (patches.db, files/), by its
# path in the title folder (build.sh copies ps5/mods to mods/ and ps5/gamedata to gamedata/).
# Commit the list with the change to a mod or a patch: consoles fetch what main's list names.
import hashlib, os, re, sys

ps5 = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
out = os.path.join(ps5, 'game-fixes.txt')
SAFE = re.compile(r'^[A-Za-z0-9_.-]+$')


def files():
    for top, title in (('mods', 'mods'), ('gamedata/files', 'gamedata/files')):
        root = os.path.join(ps5, top)
        for d, dirs, names in os.walk(root):
            dirs.sort()
            for n in sorted(names):
                if top == 'mods' and n == 'README.md':
                    continue
                rel = os.path.relpath(os.path.join(d, n), root).replace(os.sep, '/')
                yield os.path.join(d, n), title + '/' + rel
    yield os.path.join(ps5, 'gamedata', 'patches.db'), 'gamedata/patches.db'


def build():
    lines = ['# OGBox game fixes 1',
             '# <sha256> <bytes> <path in the title folder>: ps5/tools/game-fixes-index.py writes this']
    for path, title in files():
        parts = title.split('/')
        if not all(SAFE.match(p) and not p.startswith('.') for p in parts):
            sys.exit(f'game-fixes-index: {title}: only letters, digits, . _ - in names (OGBox refuses others)')
        data = open(path, 'rb').read()
        lines.append(f'{hashlib.sha256(data).hexdigest()} {len(data)} {title}')
    return '\n'.join(lines) + '\n'


text = build()
if '--check' in sys.argv[1:]:
    have = open(out).read() if os.path.exists(out) else ''
    if have != text:
        sys.exit('game-fixes-index: ps5/game-fixes.txt is not current: run ps5/tools/game-fixes-index.py')
    print('game-fixes-index: current')
else:
    open(out, 'w').write(text)
    print(f'game-fixes-index: {out} ({text.count(chr(10)) - 2} files)')
