#!/usr/bin/env python3
"""B1172 (#491): the --camera b1118 runs (tour-7 + the 40 records) before / after the
client actor scale, against B1157's committed b1118 table (refactor/B1157/b1118_table.tsv:
land_cov_n, end_n, src_cov_n, src_on25_n, src_cen_n -- the same instrument, B1139's
harness_measure.py, on fable at B1157).

usage: compare_b1118.py <runs_before> <runs_after> <out.tsv>
  <runs_*>/measure/<tag>/summary.tsv (tag tour_<sid> | b1118_r<rec>_<sid>), frames under
  <runs_*>/frames/<tag>/. frames_identical = every frame_*.png sha256-equal before/after.
Per axis: b1157, before, after, d = after - before. owner: '-' when nothing moved."""
import csv, glob, hashlib, os, sys

eb, ea, out = sys.argv[1:4]
here = os.path.dirname(os.path.abspath(__file__))
b57 = os.path.join(here, '..', '..', 'B1157', 'b1118_table.tsv')
K = {}
for r in csv.DictReader(open(b57), delimiter='\t'):
    K[(r['row'], r['spell'])] = r


def summ(ev, tag, sid):
    p = f'{ev}/measure/{tag}/summary.tsv'
    if not os.path.exists(p):
        return None
    for r in csv.DictReader(open(p), delimiter='\t'):
        if r['spell'] == sid:
            return r
    return {}


def frames_equal(tag):
    fb = sorted(glob.glob(f'{eb}/frames/{tag}/frame_*.png'))
    fa = sorted(glob.glob(f'{ea}/frames/{tag}/frame_*.png'))
    if not fb or len(fb) != len(fa):
        return f'no ({len(fb)} vs {len(fa)} frames)'
    h = lambda p: hashlib.sha256(open(p, 'rb').read()).hexdigest()
    diff = [os.path.basename(x) for x, y in zip(fb, fa) if h(x) != h(y)]
    return 'yes' if not diff else f'no ({len(diff)}/{len(fb)} differ, first {diff[0]})'


def num(x):
    try:
        return float(str(x).replace('+', ''))
    except (TypeError, ValueError):
        return None


AX = [('land_cov', 'land_cov_n'), ('end', 'end_n'), ('src_cov', 'src_cov_n'), ('src_on25', 'src_on25_n'),
      ('src_cen', 'src_cen_n')]
tags = sorted({os.path.basename(p) for p in glob.glob(f'{ea}/measure/*') if not os.path.basename(p).startswith('fixed_')})
order = lambda t: (0, int(t.split('_')[1])) if t.startswith('tour_') else (1, int(t.split('_')[1][1:]))
hdr = ['tag', 'spell', 'frames_identical']
for a, _ in AX:
    hdr += [f'b1157_{a}', f'before_{a}', f'after_{a}', f'd_{a}']
hdr += ['moved_axes']
lines = ['\t'.join(hdr)]
for tag in sorted(tags, key=order):
    sid = tag.split('_')[-1]
    key = (f'tour {sid}', sid) if tag.startswith('tour_') else (tag.split('_')[1], sid)
    k, b, a = K.get(key, {}), summ(eb, tag, sid) or {}, summ(ea, tag, sid) or {}
    row = [tag, sid, frames_equal(tag)]
    moved = []
    for ax, kax in AX:
        vb, va = b.get(ax, ''), a.get(ax, '')
        nb, na = num(vb), num(va)
        d = f'{na - nb:+.3f}' if nb is not None and na is not None else ('' if vb == va else 'n/a')
        if d not in ('', '+0.000', '-0.000'):
            moved.append(ax)
        row += [k.get(kax, ''), vb, va, d]
    row.append(','.join(moved) if moved else '-')
    lines.append('\t'.join(row))
open(out, 'w').write('\n'.join(lines) + '\n')
print('\n'.join(lines))
