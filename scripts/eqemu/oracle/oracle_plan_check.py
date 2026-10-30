#!/usr/bin/env python3
"""B1129 (#449): refuse an oracle plan whose gem casts the official client will refuse.

The official client checks a gem cast against the caster's class BEFORE it sends
anything: a class whose spells_us.txt level for the spell is 255, or above the
caster's level, gets "You are not high enough level to cast this spell!" and no
OP_CastSpell (refactor/B1118/README.md, oracle_leg/24_fx41.png). B1107 read
those refusals as click misses. This check replays the plan's gem state
(`#memspell <spell_id> <slot>`, slot 0 = gem 1; `#level N`) and, for every gem
cast -- a `click` on a gem centre of the lane's measured bar (gem_pitch.tsv) or
a `command /cast N` -- names the spell and the class when the cast cannot go.

spells_us.txt: '^'-separated, field 0 id, field 1 name, fields 104..119 the
minimum level for classes 1..16 (WAR CLR PAL RNG SHD DRU MNK BRD ROG SHM NEC WIZ
MAG ENC BST BER), 255 = cannot cast; the same indices as the willeq_db builder
(tools/willeq_db/db_loaders_text.cpp, EQ::SpellField::ClassLevel1 = 104).

It also guards the LOOT step (B1192 #511): a `command /loot` whose window after
the last `#damage` carries no corpse targeting (a `press` of F7/F8 or a
`click`/`rclick`) is refused -- the official client has no name-based corpse
targeting, so such a plan can never open the loot window.

usage:
  oracle_plan_check.py check <plan.tsv> --class N [--level N] [--lane N]
                       [--memmed SLOT:ID,...] [--memmed-file slot<TAB>id.tsv] [--spells PATH]
      exit 0 every gem cast can go and every /loot is corpse-targeted,
             1 refused (one line per offending cast or loot step), 2 error
  oracle_plan_check.py gem-xy --lane N --gem I      (gem I 1-based -> "x,y" root coords)
"""
import argparse
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CLASS_ABBR = ['WAR', 'CLR', 'PAL', 'RNG', 'SHD', 'DRU', 'MNK', 'BRD',
              'ROG', 'SHM', 'NEC', 'WIZ', 'MAG', 'ENC', 'BST', 'BER']
CLASS_LEVEL_FIELD = 104          # class 1's field; class c is 104 + (c - 1)
CANNOT_CAST = 255
GEM_HALF_WIDTH = 18              # a click within 18 px of gem_x is in the gem column
DEFAULT_SPELLS = os.path.join(os.environ.get('EQ_CLIENT_PATH', '/mnt/projects/EverQuestP1999'),
                              'spells_us.txt')


def class_name(c):
    return f'{CLASS_ABBR[c - 1]} (class {c})' if 1 <= c <= len(CLASS_ABBR) else f'class {c}'


def load_pitch(lane, table=HERE / 'gem_pitch.tsv'):
    """The lane's measured gem bar, or None (unmeasured: measure on first use)."""
    for line in Path(table).read_text().splitlines():
        if not line.strip() or line.startswith('#'):
            continue
        f = line.split('\t')
        if f[0] == str(lane):
            if f[2] == '-':
                return None
            return {'screen': f[1], 'x': int(f[2]), 'y1': int(f[3]), 'pitch': int(f[4]), 'gems': int(f[5])}
    return None


def gem_xy(bar, gem):
    return bar['x'], bar['y1'] + bar['pitch'] * (gem - 1)


def gem_at(bar, x, y):
    """1-based gem whose cell holds root (x, y), or None."""
    if abs(x - bar['x']) > GEM_HALF_WIDTH:
        return None
    for g in range(1, bar['gems'] + 1):
        if abs(y - gem_xy(bar, g)[1]) * 2 < bar['pitch']:
            return g
    return None


def load_spells(path, ids):
    """{id: (name, [16 class levels])} for the wanted ids only."""
    out = {}
    with open(path, encoding='latin-1') as fh:
        for line in fh:
            f = line.rstrip('\r\n').split('^')
            if not f[0].isdigit() or int(f[0]) not in ids or len(f) < CLASS_LEVEL_FIELD + 16:
                continue
            levels = [int(v) if v.lstrip('-').isdigit() else CANNOT_CAST
                      for v in f[CLASS_LEVEL_FIELD:CLASS_LEVEL_FIELD + 16]]
            out[int(f[0])] = (f[1], [CANNOT_CAST if v < 0 else v for v in levels])
    return out


def gem_casts(plan_lines, lane_bar, memmed, level):
    """Walk the plan; yield (line_no, gem, spell_id or None, level or None, how) per gem cast,
    and collect notes (unmeasured-lane gem-column clicks, unknown gems)."""
    gems = dict(memmed)  # 1-based gem -> spell id
    casts, notes = [], []
    for n, line in enumerate(plan_lines, 1):
        if not line.strip() or line.startswith('#'):
            continue
        f = line.split('\t')
        action, target = f[0], (f[1] if len(f) > 1 else '')
        if action == 'command':
            w = target.split()
            if w[:1] == ['#memspell'] and len(w) >= 3 and w[1].isdigit() and w[2].isdigit():
                gems[int(w[2]) + 1] = int(w[1])
            elif w[:1] == ['#level'] and len(w) >= 2 and w[1].isdigit():
                level = int(w[1])
            elif w[:1] == ['/cast'] and len(w) >= 2 and w[1].isdigit():
                g = int(w[1])
                casts.append((n, g, gems.get(g), level, f'/cast {g}'))
        elif action == 'click' and ',' in target:
            try:
                x, y = (int(v) for v in target.split(',', 1))
            except ValueError:
                continue
            if lane_bar is None:
                if x <= 2 * GEM_HALF_WIDTH + 25:
                    notes.append(f'line {n}: click {target} is in the gem column but this lane\'s gem bar '
                                 'is unmeasured (gem_pitch.tsv: measure on first use) -- not checked')
                continue
            g = gem_at(lane_bar, x, y)
            if g is not None:
                casts.append((n, g, gems.get(g), level, f'click {target} = gem {g}'))
    return casts, notes


def loot_guards(plan_lines, out=None):
    """B1192 (#511): a `/loot` step must be preceded -- after the last `#damage` --
    by corpse targeting: a `press` of F7 (TARGETPC) or F8 (TARGETNPC, the
    official client's own binding, lane-5 live keymap SLOT 32 raw 0x42000000)
    or a `click`/`rclick` on the corpse, or a `/target <name>` (the corpse
    keeps its name prefix -- `TrollF` -> `TrollF`s_corpseNNN` -- so the same
    name that targeted the live mob resolves to the corpse after death; run 6
    showed OP_Death clears HoTT, so the target must be re-set AFTER the kill).

    A plan with no post-kill targeting can never open the loot window, so it
    is refused BEFORE the client starts (exit 64 in oracle_class_leg.sh)
    instead of burning a lane-5 run. Returns the number of refused `/loot`
    lines."""
    bad = 0
    for n, line in enumerate(plan_lines, 1):
        if not line.strip() or line.startswith('#'):
            continue
        f = line.split('\t')
        if not (f[0] == 'command' and len(f) > 1 and f[1].split()[:1] == ['/loot']):
            continue
        window, last_dmg = [], 0
        for m, l2 in enumerate(plan_lines[:n - 1], 1):
            if not l2.strip() or l2.startswith('#'):
                continue
            g = l2.split('\t')
            act, tgt = g[0], (g[1] if len(g) > 1 else '')
            if act == 'command' and tgt.split()[:1] == ['#damage']:
                window, last_dmg = [], m
            elif m > last_dmg:
                window.append((m, act, tgt))
        targeted = [w for w in window
                    if (w[1] == 'press' and w[2].upper() in ('F7', 'F8'))
                    or w[1] in ('click', 'rclick')
                    or (w[1] == 'command' and w[2].split()[:1] == ['/target'])]
        if not targeted:
            print(f'REFUSED line {n}: /loot without corpse targeting -- the target must be set AFTER the last '
                  '#damage (OP_Death clears HoTT, B1192 run 6: the corpse held 120 through /loot, no 105). '
                  'Target the corpse: press F8 (TARGETNPC), click the corpse, or /target <name> (the corpse '
                  'keeps its name prefix).', file=out)
            bad += 1
        else:
            print(f'OK line {n}: /loot corpse targeting: '
                  + ', '.join(f'line {m} {act} {tgt}' for m, act, tgt in targeted), file=out)
    return bad


def check(plan, cls, level, lane, memmed, spells_path, out=None):
    out = out or sys.stdout
    lines = Path(plan).read_text().splitlines()
    loot_bad = loot_guards(lines, out)
    bar = load_pitch(lane) if lane else None
    casts, notes = gem_casts(lines, bar, memmed, level)
    for note in notes:
        print(f'NOTE {note}', file=out)
    if not casts:
        return 1 if loot_bad else 0
    ids = {sid for _, _, sid, _, _ in casts if sid is not None}
    if ids and not os.path.isfile(spells_path):
        print(f'REFUSED: the plan casts gems but spells_us.txt is missing: {spells_path}', file=out)
        return 1
    spells = load_spells(spells_path, ids) if ids else {}
    bad = 0
    for n, g, sid, lvl, how in casts:
        if sid is None:
            print(f'NOTE line {n}: {how}: gem {g} holds no spell known to the plan '
                  '(no #memspell, no --memmed) -- not checked', file=out)
            continue
        if sid not in spells:
            print(f'REFUSED line {n}: {how}: spell {sid} is not in {spells_path}', file=out)
            bad += 1
            continue
        name, levels = spells[sid]
        need = levels[cls - 1]
        if need == CANNOT_CAST:
            print(f'REFUSED line {n}: {how}: spell {sid} "{name}" cannot be cast by {class_name(cls)} '
                  f'(spells_us.txt field {CLASS_LEVEL_FIELD + cls - 1} = 255); castable by: '
                  + (', '.join(f'{CLASS_ABBR[i]} {v}' for i, v in enumerate(levels) if v != CANNOT_CAST) or 'nobody'),
                  file=out)
            bad += 1
        elif lvl is not None and need > lvl:
            print(f'REFUSED line {n}: {how}: spell {sid} "{name}" needs {class_name(cls)} level {need}, '
                  f'the character is level {lvl}', file=out)
            bad += 1
        else:
            print(f'OK line {n}: {how}: spell {sid} "{name}" {class_name(cls)} level {need}'
                  + ('' if lvl is not None else ' (character level unknown: level not checked)'), file=out)
    return 1 if (bad or loot_bad) else 0


def parse_memmed(spec, path):
    gems = {}
    if path:
        for row in Path(path).read_text().splitlines():
            f = row.split('\t')
            if len(f) >= 2 and f[0].strip().isdigit() and f[1].strip().isdigit():
                gems[int(f[0]) + 1] = int(f[1])
    for item in filter(None, (spec or '').split(',')):
        slot, sid = item.split(':')
        gems[int(slot) + 1] = int(sid)
    return gems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('check')
    c.add_argument('plan')
    c.add_argument('--class', dest='cls', type=int, required=True)
    c.add_argument('--level', type=int)
    c.add_argument('--lane', type=int)
    c.add_argument('--memmed', help='DB slot:spell_id,... (slot 0 = gem 1)')
    c.add_argument('--memmed-file', help='character_memmed_spells rows: slot_id<TAB>spell_id')
    c.add_argument('--spells', default=DEFAULT_SPELLS)
    g = sub.add_parser('gem-xy')
    g.add_argument('--lane', type=int, required=True)
    g.add_argument('--gem', type=int, required=True)
    a = ap.parse_args(argv)
    if a.cmd == 'gem-xy':
        bar = load_pitch(a.lane)
        if bar is None:
            print(f'lane {a.lane}: gem bar unmeasured (gem_pitch.tsv: measure on first use)', file=sys.stderr)
            return 2
        if not 1 <= a.gem <= bar['gems']:
            print(f'gem {a.gem} outside 1..{bar["gems"]}', file=sys.stderr)
            return 2
        print('%d,%d' % gem_xy(bar, a.gem))
        return 0
    if not 1 <= a.cls <= len(CLASS_ABBR):
        print(f'class {a.cls} outside 1..{len(CLASS_ABBR)}', file=sys.stderr)
        return 2
    try:
        memmed = parse_memmed(a.memmed, a.memmed_file)
    except ValueError as e:
        print(f'bad --memmed: {e}', file=sys.stderr)
        return 2
    return check(a.plan, a.cls, a.level, a.lane, memmed, a.spells)


if __name__ == '__main__':
    sys.exit(main())
