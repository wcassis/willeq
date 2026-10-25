#!/usr/bin/env python3
# B1150: join the 40 records' representatives with (a) their PRE-retirement
# config shapes and (b) the record's own EDD shapes (refactor/B1150/edd_shapes.tsv)
# -> the "what each shape case replaced per spell" table
# (refactor/B1150/shape_replacement_table.md).
#
# usage: join_shapes.py <worktree> <pre-retirement-commit>
#   <worktree>              the branch worktree (reads refactor/B1150/edd_shapes.tsv)
#   <pre-retirement-commit> the config/spell_effects.json to join against: the
#                           retirement commit's parent (on this branch: 9aa72ed5,
#                           the B1149 head; the retirement is 216f6621). HEAD's
#                           config already carries the retired words, so HEAD is
#                           the wrong input.
# Regenerate the committed table with:
#   python3 refactor/B1150/scripts/join_shapes.py /home/user/src/willeq-wt/kb-b1150-shapes 9aa72ed5 \
#     > refactor/B1150/shape_replacement_table.md
import csv, json, subprocess, sys

WT, PRE = sys.argv[1], sys.argv[2]
RT = '/mnt/projects/fable/review/B1130/record_table.tsv'

cfg = json.loads(subprocess.run(['git', '-C', WT, 'show', f'{PRE}:config/spell_effects.json'],
                                capture_output=True, text=True, check=True).stdout)
src = {int(k): v for k, v in cfg['sourceRecords']['records'].items()}
tgt = {int(k): v for k, v in cfg['targetRecords']['records'].items()}
ovr = cfg.get('spellOverrides', {})

def cfgrow(row):
    # A record row never carried a spawnShape: its measured "shape" is the
    # extent band (lo/hi/halfWidth/centroid) on top of its anchor.
    if not row: return ('-', '-')
    a = row.get('anchor', '-')
    if all(k in row for k in ('lo', 'hi', 'halfWidth')):
        ext = f"{row['lo']}/{row['hi']}/{row['halfWidth']}" + ("/c" + str(row['centroid']) if 'centroid' in row else '')
    elif any(k in row for k in ('discLo', 'discHi', 'spiral')):
        ext = "disc/spiral"
    else:
        ext = '-'
    return (a, ext)

def ovrshapes(spell):
    # spellOverrides: spell -> style -> {..., spawnShape?, spawnExtents?}
    o = ovr.get(str(spell), {})
    out = []
    for style, d in o.items():
        if isinstance(d, dict) and 'spawnShape' in d:
            ext = d.get('spawnExtents', '')
            out.append(f"{style}:{d['spawnShape']}{ext if ext else ''}")
    return ', '.join(out) or '-'

# record's shapes from the TSV: per (record, stage 'S'/'T'), one row per
# spellsnew.eff slot.
shapes = {}
for r in csv.DictReader((l for l in open(f'{WT}/refactor/B1150/edd_shapes.tsv') if not l.startswith('#')),
                        delimiter='\t'):
    shapes.setdefault(int(r['record']), {}).setdefault(r['stage'], []).append(int(r['shape']))

rows = []
for r in csv.DictReader(open(RT), delimiter='\t'):
    rec = int(r['record'])
    if rec == 51: continue
    sid = int(r['spell_id'])
    s_anchor, s_ext = cfgrow(src.get(rec))
    t_anchor, t_ext = cfgrow(tgt.get(rec))
    o_shapes = ovrshapes(sid)
    recs = shapes.get(rec, {})
    s_shapes = recs.get('S', [])
    t_shapes = recs.get('T', [])
    rows.append(f"| {rec} | {sid} {r['name'][:22]} | {'/'.join(map(str, s_shapes)) or '-'} | {s_anchor} | {s_ext} | "
                f"{'/'.join(map(str, t_shapes)) or '-'} | {t_anchor} | {t_ext} | {o_shapes} |")

print('| record | spell | EDD S shape | cfg src anchor | src band | EDD T shape | cfg tgt anchor | tgt band | retired override shapes |')
print('|---|---|---|---|---|---|---|---|---|')
print('\n'.join(rows))
# shape histogram of the 40 records' 299 slot rows
from collections import Counter
c = Counter()
for rec, st in shapes.items():
    if rec == 51: continue
    for lst in st.values(): c.update(lst)
print()
print('40-record shape histogram (299 slot rows):', dict(sorted(c.items())))
