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

Usage: import-gamedata.py --out ps5/gamedata --patchhub <dir with gameData.json and the .jmp files
       fetch-patchhub wrote> --icup <XboxGraphicalPatches checkout> --redump <Microsoft - Xbox.dat>

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
    '720p': '720p', '480i fix': '480p', '480 fix': '480p', '480p': '480p', 'progressive': '480p',
    '60 fps': '60 FPS', 'misc': 'Misc', 'unknown': 'Misc',
}


def category_of(text):
    t = text.lower()
    for k in ('720p', '60 fps', '480i fix', '480 fix', '480p', 'progressive', 'widescreen', '16-9', '16:9'):
        if k in t:
            return CATEGORIES[k]
    return 'Misc'


def clean(s):
    return re.sub(r'[\t\r\n]+', ' ', (s or '')).strip()


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


# ---- the import -----------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--patchhub', required=True)
    ap.add_argument('--icup', required=True)
    ap.add_argument('--redump', required=True)
    ap.add_argument('--overrides', default=None, help='name<TAB>titleid lines for IPS names left unmatched')
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
    n_jmp = n_jmp_missing = 0
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
                if p.get('ramStatus') in ('128', '128MB', 'Yes'):
                    note = (note + ' ' if note else '') + '(needs 128 MB)'
                m = re.search(r'\{([^}]*)\}', p['name'])
                name = m.group(1) if m else (p.get('patchType') or cat)
                crc = jmp_crc(head.get('version'))
                records_out.append((tid, version, pid('jmp', url), cat, clean(name), author, note,
                                    "Jay's PatchHub (jayxbox.com)", sf.get('region') or '', crc, recs))
                n_jmp += 1

    # OgXHD
    n_og = 0
    for fname, cat in (('wdb.dat', 'Widescreen'), ('hdb.dat', '720p')):
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

    records_out.sort(key=lambda r: (r[0], r[3], r[4].lower()))
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
    print('patches: %d JMP (%d not fetched), %d OgXHD, %d IPS/BPS; %d IPS/BPS without a game: %s' % (
        n_jmp, n_jmp_missing, n_og, n_ips, len(unmatched), '; '.join(unmatched)))
    print('games: %d discs' % len(redump))


if __name__ == '__main__':
    sys.exit(main())
