#!/usr/bin/env python3
"""B1167 (#486): the b1118 tour-7 + 40-record sweep before (fable f61fafb4) / after (this branch),
against B1157's committed table (refactor/B1157/b1118_table.tsv, its `_n` columns) and, for the
tour rows, the B1142 lane-5 reference (review/B1142/gate/regression_table.tsv `new_land_cov`,
`new_end`, the gate's oracle rig).

usage: compare_b1118.py <ev_before> <ev_after> <out.tsv>
  <ev_*>/measure/{tour_<sid>,b1118_r<rec>_<sid>}/summary.tsv (B1139's harness_measure.py)
A row MOVED when land_cov differs by more than 3 % (B1131's band) or end by more than 0.10 s
(B1149's) or src_cov by more than 3 %, after vs before. frames_identical: sha256 of every frame.
"""
import sys, os, glob, hashlib
eb, ea, out = sys.argv[1:4]
here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.abspath(os.path.join(here, '..', '..', '..'))
def tsv(p):
    rows = [l.rstrip('\n').split('\t') for l in open(p) if not l.startswith('#')]
    return rows[0], rows[1:]
h, rows = tsv(os.path.join(repo, 'refactor', 'B1157', 'b1118_table.tsv'))
B57 = {r[0]: dict(zip(h, r)) for r in rows}
h, rows = tsv('/mnt/projects/fable/review/B1142/gate/regression_table.tsv')
B42 = {r[0]: dict(zip(h, r)) for r in rows}
def summ(ev, tag, sid):
    p = f'{ev}/measure/{tag}/summary.tsv'
    if not os.path.exists(p): return None
    h, rows = tsv(p)
    for r in rows:
        if r[0] == sid: return dict(zip(h, r))
    return None
def f(x):
    try: return float(x)
    except Exception: return None
def ident(tag):
    fb = sorted(glob.glob(f'{eb}/frames/{tag}/frame_*.png')); fa = sorted(glob.glob(f'{ea}/frames/{tag}/frame_*.png'))
    if not fb or len(fb) != len(fa): return f'no({len(fb)}/{len(fa)})'
    H = lambda p: hashlib.sha256(open(p, 'rb').read()).hexdigest()
    d = sum(1 for x, y in zip(fb, fa) if H(x) != H(y))
    return 'yes' if d == 0 else f'no({d}/{len(fb)})'
tags = sorted({os.path.basename(p) for p in glob.glob(f'{ea}/measure/*') if os.path.basename(p).startswith(('tour_', 'b1118_'))},
              key=lambda t: (0 if t.startswith('tour') else 1, int(t.split('_')[1].lstrip('r'))))
cols = ['row', 'spell', 'frames_identical', 'land_b1157', 'land_before', 'land_after', 'd_land', 'end_b1157', 'end_before',
        'end_after', 'd_end', 'src_b1157', 'src_before', 'src_after', 'd_src', 'lane5_land', 'lane5_end', 'moved']
with open(out, 'w') as o:
    o.write('\t'.join(cols) + '\n')
    for t in tags:
        parts = t.split('_')
        if t.startswith('tour'):
            sid = parts[1]; key = f'tour {sid}'
        else:
            sid = parts[2]; key = parts[1]
        b, a, k = summ(eb, t, sid), summ(ea, t, sid), B57.get(key, {})
        if not a: continue
        g = lambda d, c: (d or {}).get(c, '')
        lb, la = f(g(b, 'land_cov')), f(g(a, 'land_cov'))
        nb, na = f(g(b, 'end')), f(g(a, 'end'))
        sb, sa = f(g(b, 'src_cov')), f(g(a, 'src_cov'))
        dl = (la - lb) if la is not None and lb is not None else None
        de = (na - nb) if na is not None and nb is not None else None
        ds = (sa - sb) if sa is not None and sb is not None else None
        mv = []
        if dl is not None and abs(dl) > 0.03 * max(lb, 1e-9): mv.append('land')
        if (la is None) != (lb is None): mv.append('land(n/a)')
        if de is not None and abs(de) > 0.10: mv.append('end')
        if ds is not None and abs(ds) > 0.03 * max(sb, 1e-9): mv.append('src')
        l5 = B42.get(sid, {}) if t.startswith('tour') else {}
        fm = lambda x: '' if x is None else f'{x:+.3f}'
        o.write('\t'.join([key, sid, ident(t), k.get('land_cov_n', ''), g(b, 'land_cov'), g(a, 'land_cov'), fm(dl),
                           k.get('end_n', ''), g(b, 'end'), g(a, 'end'), fm(de), k.get('src_cov_n', ''), g(b, 'src_cov'),
                           g(a, 'src_cov'), fm(ds), l5.get('new_land_cov', ''), l5.get('new_end', ''),
                           ','.join(mv) or '-']) + '\n')
print(open(out).read())
