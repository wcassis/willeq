#!/usr/bin/env python3
"""B1166 (#485): tour-7 on --camera b1118, before vs after -- the proof the b1118 camera did not
move. Per spell: frame count, frames byte-identical (sha256 of every frame_*.png), the summary.tsv
row (review/B1139/scripts/harness_measure.py) before and after, and B1157's committed tour row
(refactor/B1157/b1118_table.tsv, column land_cov_n / end_n).
usage: compare_tour7.py <ev_before> <ev_after> <out.tsv>"""
import sys, os, glob, hashlib
eb, ea, out = sys.argv[1:4]
repo = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
b57 = {}
rows = [l.rstrip('\n').split('\t') for l in open(os.path.join(repo, 'refactor', 'B1157', 'b1118_table.tsv'))]
for r in rows[1:]:
    d = dict(zip(rows[0], r))
    if d['row'].startswith('tour '): b57[d['spell']] = d
def summ(ev, sid):
    p = f'{ev}/measure/tour_{sid}/summary.tsv'
    ls = [l.rstrip('\n').split('\t') for l in open(p)]
    for r in ls[1:]:
        if r[0] == sid: return ls[0], r
    return ls[0], None
h = lambda p: hashlib.sha256(open(p, 'rb').read()).hexdigest()
lines = []
for sid in ['93', '38', '54', '656', '41', '253', '458']:
    fb = sorted(glob.glob(f'{eb}/frames/tour_{sid}/frame_*.png'))
    fa = sorted(glob.glob(f'{ea}/frames/tour_{sid}/frame_*.png'))
    same = len(fb) == len(fa) and all(h(x) == h(y) for x, y in zip(fb, fa))
    hdr, rb = summ(eb, sid)
    _, ra = summ(ea, sid)
    if not lines:
        lines.append('\t'.join(['spell', 'frames', 'frames_identical', 'summary_rows_identical', 'b1157_land_cov_n',
                                'b1157_end_n'] + [f'before_{c}' for c in hdr[1:]] + [f'after_{c}' for c in hdr[1:]]))
    k = b57.get(sid, {})
    lines.append('\t'.join([sid, str(len(fa)), 'yes' if same else 'NO', 'yes' if rb == ra else 'NO',
                            k.get('land_cov_n', ''), k.get('end_n', '')] + rb[1:] + ra[1:]))
open(out, 'w').write('\n'.join(lines) + '\n')
print('\n'.join(lines))
