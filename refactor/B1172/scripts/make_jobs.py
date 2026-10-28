#!/usr/bin/env python3
"""B1172: the render job list -- tour-7 + the 40 records on --camera b1118
(B1157's rows: refactor/B1157/scripts/run_sweep_b1118.sh, the tour block and
refactor/B1152/plans/sweep/list.tsv; caster level 60, t_begin 1.5 s, target
distance 0, seconds = 1.5 + cast_ms/1000 + 11.5) and the 40 B1130 records on
--camera b1130 / b1130-self (B1166's run_sweep_b1130.sh layout: the record
table's spell_id / cast_ms / caster_level, seconds = 1.5 + cast_ms/1000 + 13.5,
--sim-hz 60). One TSV row per run:
tag  camera  seconds  sim_hz  measure_kind  plan_text(\\n-escaped)
usage: make_jobs.py <repo> <record_table.tsv>"""
import csv
import sys

repo, table = sys.argv[1], sys.argv[2]
HDR = 't_begin_s\\tspell_id\\ttarget\\tdistance_units\\tcast_ms\\tcaster_level'
out = []
for sid, tgt, cms in [(93, 'other', 1500), (38, 'other', 1500), (54, 'other', 1500), (656, 'other', 1500),
                      (41, 'other', 1500), (253, 'self', 1750), (458, 'other', 3150)]:
    secs = round(1.5 + cms / 1000 + 11.5, 1)
    out.append((f'tour_{sid}', 'b1118', secs, '-', 'b1118', f'{HDR}\\n1.5\\t{sid}\\t{tgt}\\t0\\t{cms}\\t60'))
for line in open(f'{repo}/refactor/B1152/plans/sweep/list.tsv'):
    p = line.rstrip('\n').split('\t')
    if len(p) < 4:
        continue
    rec, sid, tgt, cms = p[0], p[1], p[2], int(p[3])
    secs = round(1.5 + cms / 1000 + 11.5, 1)
    out.append((f'b1118_r{rec}_{sid}', 'b1118', secs, '-', 'b1118', f'{HDR}\\n1.5\\t{sid}\\t{tgt}\\t0\\t{cms}\\t60'))
for r in csv.reader(open(table), delimiter='\t'):
    if r[0] == 'record' or r[0] == '51':
        continue
    rec, spell_id, cast_ms, caster_level, captures, targettype = r[0], r[5], int(r[9]), r[8], r[11], r[10]
    kind = 'fixed_npc' if 'fixed_npc' in captures else 'fixed_self'
    cam = 'b1130' if kind == 'fixed_npc' else 'b1130-self'
    tgt = 'self' if targettype in ('self', 'group', 'group-teleport') else 'other'
    secs = f'{1.5 + cast_ms / 1000 + 13.5:.1f}'
    out.append((f'{kind}_r{rec}', cam, secs, '60', kind,
                f'{HDR}\\tpackets\\n1.5\\t{spell_id}\\t{tgt}\\t0\\t{cast_ms}\\t{caster_level}'))
for o in out:
    print('\t'.join(str(x) for x in o))
