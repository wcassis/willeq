#!/usr/bin/env python3
"""B1171 (#490): tour-7 on --camera b1118, before (fable, B1166's literal-60 plans) vs after
(this branch, the B1118 oracle leg's executed `#level`), with the oracle leg and its level, each
run's harness level (timing.tsv caster_level, else the plan), the frames' byte identity
(sha256 of every frame_*.png), the land_cov / end / src_cov summary cells
(review/B1139/scripts/harness_measure.py), B1157's and B1166's committed tour rows
(refactor/B1157/b1118_table.tsv land_cov_n / end_n; refactor/B1166/tables/tour7_b1118.tsv after_*)
and the lane oracle columns of the B1142 gate table (review/B1142/gate/regression_table.tsv
oracle_land_cov / oracle_end). A run whose level differs from the leg's is REFUSED.
usage: compare_tour7.py <ev_before> <ev_after> <out.tsv>"""
import glob
import hashlib
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.abspath(os.path.join(here, '..', '..', '..'))
sys.path.insert(0, os.path.join(repo, 'scripts', 'fx'))
import capture_level as CL  # noqa: E402

eb, ea, out = sys.argv[1:4]


def table(p, key):
    rows = [l.rstrip('\n').split('\t') for l in open(p) if not l.startswith('#')]
    return {dict(zip(rows[0], r))[key]: dict(zip(rows[0], r)) for r in rows[1:]}


b57 = {d['spell']: d for d in table(os.path.join(repo, 'refactor', 'B1157', 'b1118_table.tsv'), 'row').values()
       if d['row'].startswith('tour ')}
b66 = table(os.path.join(repo, 'refactor', 'B1166', 'tables', 'tour7_b1118.tsv'), 'spell')
lane = table('/mnt/projects/fable/review/B1142/gate/regression_table.tsv', 'spell')


def summ(ev, sid):
    ls = [l.rstrip('\n').split('\t') for l in open(f'{ev}/measure/tour_{sid}/summary.tsv')]
    for r in ls[1:]:
        if r[0] == sid:
            return dict(zip(ls[0], r))
    return {}


h = lambda p: hashlib.sha256(open(p, 'rb').read()).hexdigest()  # noqa: E731
COLS = ['land_cov', 'end', 'src_cov']
hdr = ['spell', 'oracle_leg', 'oracle_level', 'before_level', 'after_level', 'frames', 'frames_identical',
       'lane_oracle_land_cov', 'lane_oracle_end', 'b1157_land_cov_n', 'b1157_end_n']
hdr += [f'b1166_{c}' for c in COLS] + [f'before_{c}' for c in COLS] + [f'after_{c}' for c in COLS]
hdr += ['before_verdict', 'after_verdict']
lines = ['\t'.join(hdr)]
refused = []
for sid in ['93', '38', '54', '656', '41', '253', '458']:
    leg, olv = CL.b1118_spell_capture(sid), CL.b1118_spell_level(sid)
    blv, _ = CL.harness_level(f'{eb}/frames/tour_{sid}', f'{eb}/plans/tour_{sid}.plan')
    alv, _ = CL.harness_level(f'{ea}/frames/tour_{sid}', f'{ea}/plans/tour_{sid}.plan')
    fb = sorted(glob.glob(f'{eb}/frames/tour_{sid}/frame_*.png'))
    fa = sorted(glob.glob(f'{ea}/frames/tour_{sid}/frame_*.png'))
    nd = sum(1 for x, y in zip(fb, fa) if h(x) != h(y))
    same = 'yes' if fb and len(fb) == len(fa) and nd == 0 else f'no ({nd}/{len(fa)})'
    sb, sa, k, s, ln = summ(eb, sid), summ(ea, sid), b57.get(sid, {}), b66.get(sid, {}), lane.get(sid, {})
    vb = CL.verdict_or_refusal(olv, blv, 'level-matched')
    va = CL.verdict_or_refusal(olv, alv, 'level-matched')
    if va != 'level-matched':
        refused.append(sid)
    row = [sid, leg, str(olv), str(blv), str(alv), str(len(fa)), same, ln.get('oracle_land_cov', ''),
           ln.get('oracle_end', ''), k.get('land_cov_n', ''), k.get('end_n', '')]
    row += [s.get(f'after_{c}', '') for c in COLS] + [sb.get(c, '') for c in COLS] + [sa.get(c, '') for c in COLS]
    row += [vb, va]
    lines.append('\t'.join(row))
open(out, 'w').write('\n'.join(lines) + '\n')
print('\n'.join(lines))
if refused:
    print(f'REFUSED after rows: {refused}', file=sys.stderr)
    sys.exit(3)
