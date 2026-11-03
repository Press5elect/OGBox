#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
"""OGBox 0.1.45: the game data the title ships in gamedata/ (ps5/gamedata/ in the tree).

  games.db    the Xbox discs Redump knows (libretro-database's metadat/redump/Microsoft - Xbox.dat):
              title ID (from the serial: "EA-013" is 0x4541000D), region, languages, the name
              libretro's thumbnails are named by.
  patches.db  graphical and other XBE patches, by title ID, from
              - Jay's PatchHub (jayxbox.com, Jay's Magic Patcher .JMP files, authors credited in each),
              - icup321's XboxGraphicalPatches (IPS/BPS files, made for the USA releases),
              - OgXHD's databases (hdb.dat, wdb.dat, in icup321's repository).
  files/      the IPS and BPS files patches.db names.

0.1.52: no resolution patches (720p, 1080i, "128mb D3D": the emulator's Resolution scale does it
without changing the game's memory layout), none that need a hard disk install or downloads, the
categories from the patches' notes (PatchHub files most 60 FPS patches under Misc), PatchHub's
ramStatus ("yes": needs 128 MB), Jay's GitHub files that PatchHub's site doesn't list (--jmp-repo),
and the same patch from several places merged into one entry that credits the others.

Usage: import-gamedata.py --out ps5/gamedata --patchhub <dir with gameData.json and the .jmp files
       fetch-patchhub wrote> --jmp-repo <Xbox-Magic-Patches-by-Jay checkout> --icup <XboxGraphicalPatches
       checkout> --redump <Microsoft - Xbox.dat> [--overrides ps5/tools/gamedata-overrides.txt]

patches.db, one record a line, fields split by tabs (ps5/src/core/ogbox_patches.c reads it):
  P  titleid  version|*  id  category  name  author  note  source  region
  T  file          the target of the records after it (default.xbe until one)
  C  crc32         the target's CRC-32 before the patch (a release check)
  F  find  repl    the first occurrence of find becomes repl (after the last F's match onward)
  O  offset  hex   the bytes at a file offset
  A  hex           bytes appended to the file
  I  file          an IPS file of files/
  B  file          a BPS file of files/
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import sys

CATEGORIES = {
    'widescreen': 'Widescreen', '16-9': 'Widescreen', '16:9': 'Widescreen',
    '720p': '720p', '1080i': '720p', '480i fix': '480i fix', '480 fix': '480i fix', '480p': '480p',
    'progressive': '480p', '60 fps': '60 FPS', '60fps': '60 FPS', 'misc': 'Misc', 'unknown': 'Misc',
}
# 0.1.52: resolution patches are left out: the emulator's Resolution scale renders the game's own frame
# larger (sharper, at no risk), while 720p patches move the game's memory layout (most clear the XBE's
# 64 MB limit), multiply the GPU's work with the scale and are the patches most often noted as broken.
DROPPED = ('720p',)


def category_of(text):
    t = text.lower()
    for k in ('720p', '1080i', '60 fps', '60fps', '480i fix', '480 fix', '480p', 'progressive', 'widescreen', '16-9',
              '16:9'):
        if k in t:
            return CATEGORIES[k]
    return 'Misc'


def clean(s):
    import html
    s = re.sub(r'<br\s*/?>', ' ', s or '', flags=re.I)
    s = html.unescape(re.sub(r'<[^>]+>', '', s))
    return re.sub(r'\s+', ' ', s).strip()


# 0.1.52: patches that do nothing useful in OGBox, which runs the disc image: the HDD / FATX fixes
# (file names shortened for a hard disk's file system: on the disc the game would no longer find its
# files), patches that need files downloaded separately (mods, translations), and patches of an XBE
# the disc doesn't have (a title update's).
NOT_FOR_OGBOX = re.compile(r'\bHDD\b|FATX|download|title update|not to be applied', re.I)


def norm(name):
    """A game name for matching: lower case words, no region groups, no punctuation."""
    n = name.lower()
    n = n.replace('&', ' and ')
    n = re.sub(r"[’'`]", '', n)
    n = re.sub(r'\bthe\b', ' ', n)
    n = re.sub(r'\b(disneys?|pixars?|dreamworks|nickelodeon|tom clancys?|marvel|star wars -)\b', ' ', n)
    n = re.sub(r'[^a-z0-9]+', ' ', n)
    return ' '.join(n.split())


# ---- Redump ---------------------------------------------------------------------------------------

def serial_title_id(serial):
    m = re.match(r'^([A-Z0-9])([A-Z0-9])-(\d{3})', serial)
    if not m:
        return None
    return '%02X%02X%04X' % (ord(m.group(1)), ord(m.group(2)), int(m.group(3)))


def read_redump(path):
    games = []
    text = open(path, encoding='utf-8').read()
    for block in re.findall(r'game \((.*?)\n\)', text, re.S):
        name = re.search(r'\bname "([^"]*)"', block).group(1)
        region = (re.search(r'\bregion "([^"]*)"', block) or [None, ''])[1]
        serial = (re.search(r'\bserial "([^"]*)"', block) or [None, ''])[1]
        langs = ''
        m = re.search(r'\((?:[A-Z][a-z](?:,[A-Z][a-z])+)\)', name)
        if m:
            langs = m.group(0)[1:-1]
        ids = []
        for s in serial.split(','):
            tid = serial_title_id(s.strip())
            if tid and tid not in ids:
                ids.append(tid)
        games.append({'name': name, 'region': region, 'serial': serial, 'ids': ids, 'langs': langs})
    return games


def base_name(redump_name):
    return re.sub(r'\s*\([^)]*\)', '', redump_name).strip()


# ---- JMP ------------------------------------------------------------------------------------------

def parse_jmp(text):
    head = {}
    records = []        # (kind, ...)
    target = None
    comment = None
    lines = [l.rstrip('\r') for l in text.split('\n')]
    i = 0
    hexre = re.compile(r'^[0-9A-Fa-f]+$')
    while i < len(lines):
        line = lines[i].strip()
        i += 1
        if not line:
            continue
        if line.startswith('#'):
            comment = line[1:].strip()
            m = re.match(r'^\[?-?\]?\s*([A-Za-z0-9_ .\\/-]+\.xbe)\s*-', comment, re.I)
            target = m.group(1).strip() if m else target
            continue
        if '=' in line and not records and not hexre.match(line) and ':' not in line.split('=')[0]:
            k, v = line.split('=', 1)
            head[k.strip().lower()] = v.strip()
            continue
        if comment is not None and '[-]' in comment:
            # a record off by default (an alternative): left out
            if hexre.match(line) and i < len(lines) and hexre.match(lines[i].strip() or 'x'):
                i += 1
            continue
        if line.upper().startswith('APPEND:'):
            data = re.sub(r'\s', '', line[7:])
            if hexre.match(data) and len(data) % 2 == 0:
                records.append(('A', target, data.upper()))
            continue
        if ':' in line:
            off, data = line.split(':', 1)
            off = off.strip().lower()
            data = re.sub(r'\s', '', data)
            if off.startswith('0x'):
                off = off[2:]
            if hexre.match(off) and hexre.match(data) and len(data) % 2 == 0:
                records.append(('O', target, '%X' % int(off, 16), data.upper()))
            continue
        if hexre.match(line) and i < len(lines):
            repl = lines[i].strip()
            i += 1
            if hexre.match(repl) and len(repl) == len(line) and len(line) % 2 == 0:
                records.append(('F', target, line.upper(), repl.upper()))
            continue
    return head, records


def jmp_crc(version):
    m = re.search(r'\{([0-9A-Fa-f]{8})\}', version or '')
    return m.group(1).upper() if m else None


# ---- OgXHD ----------------------------------------------------------------------------------------

def parse_ogxhd(path):
    entries = []
    name = None
    cur = None
    pending = None
    for raw in open(path, encoding='utf-8', errors='replace'):
        line = raw.strip()
        if not line:
            continue
        if line.startswith("'"):
            name = line[1:].strip()
            continue
        if line.startswith(':'):
            if line.lower() == ':end':
                break
            cur = {'id': line[1:].strip().upper(), 'name': name or '', 'pairs': []}
            entries.append(cur)
            pending = None
            continue
        if cur is None or not re.match(r'^[0-9A-Fa-f]+$', line):
            continue
        if pending is None:
            pending = line.upper()
        else:
            if len(pending) == len(line) and len(line) % 2 == 0:
                cur['pairs'].append((pending, line.upper()))
            pending = None
    return entries


# ---- 0.1.52: the same patch from several places, merged ------------------------------------------

def ips_changes(path):
    """An IPS file's writes: {(offset, byte)} (the truncation extension aside)."""
    d = open(path, 'rb').read()
    out = set()
    i = 5
    while i + 3 <= len(d) and d[i:i + 3] != b'EOF':
        off = int.from_bytes(d[i:i + 3], 'big')
        n = int.from_bytes(d[i + 3:i + 5], 'big')
        i += 5
        if n == 0:
            run = int.from_bytes(d[i:i + 2], 'big')
            data = d[i + 2:i + 3] * run
            i += 3
        else:
            data = d[i:i + n]
            i += n
        out.update((off + k, b) for k, b in enumerate(data))
    return out


def shape(rec, files):
    """What a patch writes, for comparing: ('O', {(target, offset, byte)}) for offset patches (JMP
    offsets, IPS), ('F', [(target, find, repl)]) for find-and-replace ones (with appends), else None."""
    offs, finds = set(), []
    for r in rec[10]:
        t = (r[1] or 'default.xbe').lower()
        if r[0] == 'O':
            off = int(r[2], 16)
            offs.update((t, off + k, b) for k, b in enumerate(bytes.fromhex(r[3])))
        elif r[0] == 'I':
            offs.update((t, o, b) for o, b in ips_changes(os.path.join(files, r[2])))
        elif r[0] == 'F':
            finds.append((t, r[2], r[3]))
        elif r[0] == 'A':
            finds.append((t, 'APPEND', r[2]))
        else:
            return None
    if offs and finds:
        return ('M', (frozenset(offs), tuple(finds)))
    return ('O', frozenset(offs)) if offs else ('F', finds) if finds else None


def same_find(a, b):
    """One find-and-replace the same change as another: the shorter's pattern inside the longer's,
    replaced the same way, and the longer changing nothing outside it."""
    ta, fa, ra = a
    tb, fb, rb = b
    if ta != tb:
        return False
    if fa == 'APPEND' or fb == 'APPEND':
        return fa == fb and ra == rb
    if len(fa) > len(fb):
        fa, ra, fb, rb = fb, rb, fa, ra
    at = 0
    while True:
        k = fb.find(fa, at)
        if k < 0:
            return False
        if k % 2 == 0 and rb[k:k + len(fa)] == ra and rb[:k] == fb[:k] and rb[k + len(fa):] == fb[k + len(fa):]:
            return True
        at = k + 1


def finds_within(small, big):
    """Every find-and-replace of one patch the same as one of another's."""
    used = set()
    for x in small:
        hit = next((j for j, y in enumerate(big) if j not in used and same_find(x, y)), None)
        if hit is None:
            return False
        used.add(hit)
    return True


def covers(a, sa, b, sb):
    """b does what a does (and maybe a little more): 1 when b covers a, 2 when a covers b, 0 neither."""
    if not sa or not sb or sa[0] == 'M' or sb[0] == 'M':
        return 0
    if sa[0] == 'O' and sb[0] == 'O':
        return 1 if sa[1] <= sb[1] else 2 if sb[1] <= sa[1] else 0
    if sa[0] == 'F' and sb[0] == 'F':
        if len(sa[1]) <= len(sb[1]) and finds_within(sa[1], sb[1]):
            return 1
        if len(sb[1]) < len(sa[1]) and finds_within(sb[1], sa[1]):
            return 2
        return 0
    if sa[0] == 'O' and sb[0] == 'F':
        return 1 if ips_is_find(sa, sb) else 0
    return 1 if ips_is_find(sb, sa) else 0


def same_patch(a, sa, b, sb):
    if a[9] and b[9] and a[9] != b[9]:
        return False                        # made for two releases
    if a[3] != b[3] or not sa or not sb or sa[0] != sb[0]:
        return False
    if sa[0] in ('O', 'M'):
        return sa[1] == sb[1]
    fa, fb = sa[1], sb[1]
    if len(fa) != len(fb):
        return False
    used = set()
    for x in fa:
        hit = next((j for j, y in enumerate(fb) if j not in used and same_find(x, y)), None)
        if hit is None:
            return False
        used.add(hit)
    return True


def ips_is_find(ips_shape, find_shape):
    """An IPS file (offsets) and a find-and-replace patch the same change, as far as can be told
    without the game's XBE: every byte run the find-and-replace changes is written by the IPS, and
    the IPS writes little more than that."""
    if not ips_shape or not find_shape or ips_shape[0] != 'O' or find_shape[0] != 'F':
        return False
    offs = sorted(ips_shape[1])
    runs, cur, last = [], bytearray(), None
    for t, o, b in offs:
        if last is not None and t == last[0] and o == last[1] + 1:
            cur.append(b)
        else:
            if cur:
                runs.append(bytes(cur))
            cur = bytearray([b])
        last = (t, o)
    if cur:
        runs.append(bytes(cur))
    changed = 0
    for t, f, r in find_shape[1]:
        if f == 'APPEND':
            return False
        fb, rb = bytes.fromhex(f), bytes.fromhex(r)
        diff = [k for k in range(len(fb)) if fb[k] != rb[k]]
        if not diff:
            continue
        core = rb[diff[0]:diff[-1] + 1]
        changed += len(core)
        if not any(core in run for run in runs):
            return False
    return changed > 0 and len(offs) <= changed * 2 + 8


MERGE_LOG = []


def merge_duplicates(records, files):
    """Patches of one game that write the same bytes (the same PatchHub file listed under a version
    and under "any", Nemesis2000's widescreen patches again in OgXHD's database, icup321's IPS files
    again as PatchHub files, one author's find pattern inside another's) become one: the one with
    the release check (a CRC-32) and the most specific pattern, crediting the others in its notes."""
    by_game = {}
    for r in records:
        by_game.setdefault(r[0], []).append(r)
    out, merged = [], 0
    for tid, recs in by_game.items():
        shapes = [shape(r, files) for r in recs]
        groups = []
        for i, r in enumerate(recs):
            for g in groups:
                j = g[0]
                if same_patch(recs[j], shapes[j], r, shapes[i]):
                    g.append(i)
                    break
            else:
                groups.append([i])
        # a compilation's copy (icup321's IPS files, OgXHD's database) of a PatchHub patch, in another
        # format or with a record more or less: one entry, the fuller of the two kept
        prefer = set()
        for g in groups:
            if not g or len(g) != 1:
                continue
            i = g[0]
            icup = recs[i][7].startswith("icup321's")
            if not (icup or recs[i][7].startswith('OgXHD')):
                continue
            for h in groups:
                if not h or h is g:
                    continue
                j = h[0]
                partner = 'PatchHub' in recs[j][7] or 'Magic Patches' in recs[j][7] or (icup and recs[j][7].startswith('OgXHD'))
                if recs[j][3] != recs[i][3] or not partner:
                    continue
                if icup and not any(recs[k][8] in ('', 'USA', 'GLO', 'USA-JPN') for k in h):
                    continue                    # icup321's are made for the USA releases
                c = covers(recs[i], shapes[i], recs[j], shapes[j])
                if c:
                    h.append(i)
                    if c == 2:
                        prefer.add(recs[i][2])  # the copy does more: it is the one kept
                    g.clear()
                    break
        groups = [g for g in groups if g]
        for g in groups:
            if len(g) == 1:
                out.append(recs[g[0]])
                continue
            members = [recs[i] for i in g]

            def rank(m):
                pattern = sum(len(x[2]) for x in m[10] if x[0] == 'F')
                return (m[2] in prefer, m[9] is not None, ('PatchHub' in m[7] or 'Magic Patches' in m[7]), m[1] != '*', pattern)
            best = max(members, key=rank)
            versions = {m[1] for m in members}
            crcs = {m[9] for m in members}
            # the release check kept only when every copy has it (a copy without one was published for
            # any release its pattern is found in); the version then decides nothing more
            crc = best[9] if None not in crcs and len(crcs) == 1 else None
            version = best[1] if len(versions) == 1 else '*'
            others = []
            for m in members:
                who = m[5] if 'OgXHD' not in m[7] else "OgXHD's database"
                if m is not best and who != best[5] and who not in others:
                    others.append(who)
            note = best[6]
            if others:
                note = (note + ' ' if note else '') + '(also by %s)' % ', '.join(others)
            # copies made for several releases: the region check left to the find pattern (and the CRC)
            region = best[8] if len({m[8] for m in members}) == 1 else ''
            out.append((best[0], version, best[2], best[3], best[4], best[5], note, best[7], region, crc, best[10]))
            merged += len(members) - 1
            MERGE_LOG.append((best, [m for m in members if m is not best]))
            for m in members:
                if m is not best:
                    for x in m[10]:
                        if x[0] in ('I', 'B') and not any(y[0] in ('I', 'B') and y[2] == x[2] for y in best[10]):
                            try:
                                os.remove(os.path.join(files, x[2]))
                            except OSError:
                                pass
    return out, merged


# ---- the import -----------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--patchhub', required=True)
    ap.add_argument('--icup', required=True)
    ap.add_argument('--redump', required=True)
    ap.add_argument('--overrides', default=None, help='name<TAB>titleid lines for IPS names left unmatched')
    ap.add_argument('--merge-log', default=None, help='a file listing each merged patch and its copies')
    ap.add_argument('--jmp-repo', default=None, help="a checkout of JayYardley/Xbox-Magic-Patches-by-Jay: its "
                    "files that PatchHub's site doesn't list are added (title ID and CRC from their version= line)")
    a = ap.parse_args()

    out = a.out
    os.makedirs(os.path.join(out, 'files'), exist_ok=True)
    for f in os.listdir(os.path.join(out, 'files')):
        os.remove(os.path.join(out, 'files', f))

    redump = read_redump(a.redump)
    by_norm = {}            # norm name -> [title ids]
    names_of = {}           # title id -> a name

    def note_name(name, tid, prefer=False):
        k = norm(name)
        if not k or not tid:
            return
        lst = by_norm.setdefault(k, [])
        if prefer:
            if tid in lst:
                lst.remove(tid)
            lst.insert(0, tid)
        elif tid not in lst:
            lst.append(tid)
        if tid not in names_of or prefer:
            names_of[tid] = name

    # games.db
    with open(os.path.join(out, 'games.db'), 'w', encoding='utf-8') as g:
        g.write('# OGBox: the Xbox discs Redump knows (libretro-database, metadat/redump/Microsoft - Xbox.dat)\n')
        g.write('# titleid\tregion\tlanguages\tserial\tname (libretro\'s thumbnails are named by it)\n')
        for e in redump:
            for tid in e['ids']:
                g.write('%s\t%s\t%s\t%s\t%s\n' % (tid, e['region'], e['langs'], e['serial'], e['name']))
            # a USA retail disc's name first (not a beta, demo, kiosk or prototype)
            extra = [g for g in re.findall(r'\(([^)]*)\)', e['name'])
                     if not re.match(r'^(USA|World|Europe|Japan|Asia|Korea|.*,.*|Rev \d+|En|Ja|Fr|De|Es|It)$', g)]
            usa = (e['region'] in ('USA', 'World') or 'USA' in e['region']) and not extra
            for tid in e['ids']:
                note_name(base_name(e['name']), tid, prefer=usa)

    records_out = []        # (titleid, version, P-fields..., [records])
    seen_ids = set()

    def pid(*parts):
        h = hashlib.sha1('\x1f'.join(parts).encode()).hexdigest()[:8]
        while h in seen_ids:
            h = hashlib.sha1((h + 'x').encode()).hexdigest()[:8]
        seen_ids.add(h)
        return 'p' + h

    # PatchHub
    data = json.load(open(os.path.join(a.patchhub, 'gameData.json'), encoding='utf-8'))
    import urllib.parse
    n_jmp = n_jmp_missing = n_dropped = n_unfit = 0
    for game in data:
        for sf in game['subfolders']:
            tid = (sf.get('titleId') or '').upper()
            if not re.match(r'^[0-9A-F]{8}$', tid):
                continue
            note_name(game['name'], tid)
            for p in sf['patchFolders']:
                if not p.get('hasJmp'):
                    continue
                url = 'https://jayxbox.com' + p['folderPath'] + '/' + urllib.parse.quote(p['jmpFileName'])
                path = os.path.join(a.patchhub, hashlib.sha1(url.encode()).hexdigest()[:16] + '.jmp')
                if not os.path.exists(path):
                    n_jmp_missing += 1
                    continue
                head, recs = parse_jmp(open(path, encoding='utf-8', errors='replace').read())
                if not recs:
                    continue
                version = sf.get('version') or '*'
                version = version if re.match(r'^\d+$', version) else '*'
                cat = category_of(p.get('patchType') or p['name'])
                author = clean(p.get('author') or head.get('author') or 'Author unknown')
                note = clean(p.get('note') or head.get('notes') or '')
                if cat == 'Misc':
                    # 0.1.52: PatchHub files most 60 FPS patches (and a few others) under Misc; their name or notes say
                    cat2 = category_of(p['name'] + ' ' + note)
                    cat = cat2 if cat2 in ('60 FPS', 'Widescreen', '720p', '480i fix', '480p') else cat
                if cat in DROPPED:
                    n_dropped += 1
                    continue
                if NOT_FOR_OGBOX.search(p['name'] + ' ' + note):
                    n_unfit += 1
                    continue
                # 0.1.52: PatchHub's ramStatus is "yes", "no" or "?" (0.1.45 looked for "Yes")
                if str(p.get('ramStatus') or '').strip().lower() in ('yes', '128', '128mb'):
                    note = (note + ' ' if note else '') + '(needs 128 MB)'
                m = re.search(r'\{([^}]*)\}', p['name'])
                name = m.group(1) if m else (p.get('patchType') or cat)
                crc = jmp_crc(head.get('version'))
                records_out.append((tid, version, pid('jmp', url), cat, clean(name), author, note,
                                    "Jay's PatchHub (jayxbox.com)", sf.get('region') or '', crc, recs))
                n_jmp += 1

    # 0.1.52: Jay's GitHub repository (the source behind PatchHub, which xemu Modern GUI downloads
    # from): the files the site doesn't list
    n_repo = n_unmatched_repo = 0
    if a.jmp_repo:
        listed = set()
        for game in data:
            for sf in game['subfolders']:
                for p in sf['patchFolders']:
                    listed.add(p['jmpFileName'].lower())
        for root, dirs, fnames in os.walk(a.jmp_repo):
            dirs[:] = [d for d in dirs if not d.startswith('.')]
            for fname in sorted(fnames):
                if not fname.lower().endswith('.jmp') or fname.lower() in listed:
                    continue
                text = open(os.path.join(root, fname), encoding='utf-8', errors='replace').read()
                head, recs = parse_jmp(text)
                m = re.match(r'\s*([0-9A-Fa-f]{8})\b', head.get('version', ''))
                if not recs or not m:
                    continue
                tid = m.group(1).upper()
                # the version= line's title ID checked against the game it names (two files carry another
                # game's: Batman Begins under NHL 2002's, Outlaw Tennis under Crash Bandicoot's)
                title = head.get('game-title', '')
                squash = lambda t: re.sub(r'[^a-z0-9]', '', t.lower())
                known = names_of.get(tid)
                if title and known and not (squash(title)[:8] in squash(known) or squash(known)[:8] in squash(title)):
                    cands = by_norm.get(norm(title), [])
                    if not cands:
                        n_unmatched_repo += 1
                        continue
                    tid = cands[0]
                kind = re.search(r'\{([^}]*)\}', fname)
                kind = kind.group(1) if kind else 'Misc'
                if re.search(r'128\s*mb', kind, re.I):
                    n_dropped += 1      # D3D made to use a 128 MB console's memory: OGBox gives 64 MB
                    continue
                note = clean(head.get('notes') or '')
                cat = category_of(kind + ' ' + note)
                if cat in DROPPED:
                    n_dropped += 1
                    continue
                if NOT_FOR_OGBOX.search(fname + ' ' + note):
                    n_unfit += 1
                    continue
                author = clean(head.get('author') or (re.search(r'\[([^\]]*)\]', fname) or [None, 'Author unknown'])[1])
                region = clean(head.get('region') or '')
                rel = os.path.relpath(os.path.join(root, fname), a.jmp_repo)
                records_out.append((tid, '*', pid('jmp-repo', rel), cat, clean(kind), author, note,
                                    "Jay's Magic Patches (GitHub)", region, jmp_crc(head.get('version')), recs))
                n_repo += 1

    # OgXHD
    n_og = 0
    for fname, cat in (('wdb.dat', 'Widescreen'), ('hdb.dat', '720p')):
        if cat in DROPPED:
            n_dropped += sum(1 for e in parse_ogxhd(os.path.join(a.icup, '!OgXHD', fname)) if e['pairs'])
            continue
        path = os.path.join(a.icup, '!OgXHD', fname)
        for e in parse_ogxhd(path):
            if not e['pairs'] or not re.match(r'^[0-9A-F]{8}$', e['id']):
                continue
            if e['name']:
                note_name(e['name'], e['id'])
            recs = [('F', None, f, r) for f, r in e['pairs']]
            title = names_of.get(e['id'], e['name'] or e['id'])
            records_out.append((e['id'], '*', pid('ogxhd', fname, e['id']), cat,
                                cat, 'OgXHD', '',
                                "OgXHD (icup321's XboxGraphicalPatches)", '', None, recs))
            n_og += 1

    # icup321's IPS / BPS
    overrides = {}
    if a.overrides and os.path.exists(a.overrides):
        for line in open(a.overrides, encoding='utf-8'):
            if line.strip() and not line.startswith('#') and '\t' in line:
                k, v = line.rstrip('\n').split('\t', 1)
                overrides[k.strip()] = v.strip().upper()
    n_ips = 0
    unmatched = []
    for folder in sorted(os.listdir(a.icup)):
        fdir = os.path.join(a.icup, folder)
        if not os.path.isdir(fdir) or folder.startswith(('!', '.')):
            continue
        for fname in sorted(os.listdir(fdir)):
            low = fname.lower()
            if not (low.endswith('.ips') or low.endswith('.bps')):
                continue
            stem = fname[:-4]
            groups = re.findall(r'\(([^()]*)\)', stem)
            game = stem
            target = None
            desc = []
            # trailing groups that say what the patch is or where it goes
            while True:
                m = re.search(r'\s*\(([^()]*)\)\s*$', game)
                if not m:
                    break
                g = m.group(1)
                if re.search(r'patch|fix|apply|rendering|issues|v\d|\bmb\b|^\d+-\d+$|prototype', g, re.I):
                    t = re.match(r'apply to (.+)$', g, re.I)
                    if t:
                        target = t.group(1).strip()
                    else:
                        desc.insert(0, g)
                    game = game[:m.start()]
                else:
                    break
            game = game.strip()
            tid = overrides.get(stem) or overrides.get(game)
            if not tid:
                cands = by_norm.get(norm(game), [])
                if not cands:
                    # a name with its subtitle left out, or the other way round
                    ng = norm(game)
                    for k, v in by_norm.items():
                        if k.startswith(ng + ' '):
                            cands = v
                            break
                tid = cands[0] if cands else None
            if not tid:
                unmatched.append(stem)
                continue
            safe = re.sub(r'[^A-Za-z0-9._-]+', '_', stem)[:120] + low[-4:]
            shutil.copyfile(os.path.join(fdir, fname), os.path.join(out, 'files', safe))
            cat = category_of(folder + ' ' + ' '.join(desc))
            if cat in DROPPED:
                os.remove(os.path.join(out, 'files', safe))
                n_dropped += 1
                continue
            note = 'Made for the USA release.'
            if target:
                note += ' Patches %s.' % target
            extra = [d for d in desc if not re.search(r'^(16-9 )?(widescreen|720p|480p|60 fps)( sound)? patch$', d, re.I)]
            if extra:
                note += ' ' + '; '.join(extra) + '.'
            recs = [('B' if low.endswith('.bps') else 'I', target, safe)]
            name = desc[0] if desc else cat
            records_out.append((tid, '*', pid('icup', folder, fname), cat, clean(name), 'icup321', note,
                                "icup321's XboxGraphicalPatches", 'USA', None, recs))
            n_ips += 1

    records_out, merged = merge_duplicates(records_out, os.path.join(out, 'files'))
    records_out.sort(key=lambda r: (r[0], r[3], r[4].lower()))
    if a.merge_log:
        with open(a.merge_log, 'w', encoding='utf-8') as f:
            for best, rest in MERGE_LOG:
                f.write('%s %s v%s %s [%s, %s, %s, crc %s]\n' % (best[0], best[2], best[1], best[3], best[5], best[7], best[8], best[9]))
                for m in rest:
                    f.write('    = %s v%s %s [%s, %s, %s, crc %s]\n' % (m[2], m[1], m[3], m[5], m[7], m[8], m[9]))
    with open(os.path.join(out, 'patches.db'), 'w', encoding='utf-8') as f:
        f.write('# OGBox: XBE patches by title ID (ps5/tools/import-gamedata.py; the format is in its header)\n')
        for (tid, version, id_, cat, name, author, note, source, region, crc, recs) in records_out:
            f.write('P\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' % (tid, version, id_, cat, name, author, note, source,
                                                               region))
            if crc:
                f.write('C\t%s\n' % crc)
            target = None
            for r in recs:
                t = r[1] or 'default.xbe'
                if t != target:
                    f.write('T\t%s\n' % t)
                    target = t
                f.write('\t'.join([r[0]] + list(r[2:])) + '\n')
    print('patches: %d JMP (%d not fetched), %d more from the JMP repository, %d OgXHD, %d IPS/BPS; %d IPS/BPS without a '
          'game: %s; %d repository files for a game not found' % (n_jmp, n_jmp_missing, n_repo, n_og, n_ips, len(unmatched),
          '; '.join(unmatched), n_unmatched_repo))
    print('patches: %d resolution patches left out, %d that need a hard disk install or downloads; %d copies merged '
          'into the patch they copy; %d in patches.db' % (n_dropped, n_unfit, merged, len(records_out)))
    cats = {}
    for r in records_out:
        cats[r[3]] = cats.get(r[3], 0) + 1
    print('patches by kind: ' + ', '.join('%s %d' % kv for kv in sorted(cats.items())))
    print('games: %d discs' % len(redump))


if __name__ == '__main__':
    sys.exit(main())
