#!/usr/bin/env python3
"""B1172 (#491), copied from B1166's compare_b1130.py (branch task/kb-b1166-target-fit) with
the evidence dirs renamed: the b1130 40-record sweep before/after the client actor scale, vs the oracle
(review/B1130/record_table.tsv via B1155's band_table.py) and vs B1157's committed banded
table (refactor/B1157/b1130_table.tsv.after.tsv).

usage: compare_b1130.py <ev_before> <ev_after> <out.tsv>
  <ev_*> = review/B1172/worker/runs_{before,after} (measure/<kind>_r<rec>/b1130_rows.tsv,
  frames/<kind>_r<rec>/frame_*.png). Writes <out>.before.tsv / <out>.after.tsv (band_table.py's
  own output) and <out> (one row per record):
  record kind frames_identical | per axis: oracle, b1157, before, after, d(after-before),
  v_before, v_after | moved
frames_identical = every frame_*.png byte-identical before/after (sha256), the b1130-self proof.
"""
import sys, os, subprocess, hashlib, glob, math
eb, ea, out = sys.argv[1:4]
here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.abspath(os.path.join(here, '..', '..', '..'))
band = os.path.join(repo, 'refactor', 'B1155', 'scripts', 'band_table.py')
b57 = os.path.join(repo, 'refactor', 'B1157', 'b1130_table.tsv.after.tsv')
for ev, o in ((eb, out + '.before.tsv'), (ea, out + '.after.tsv')):
    subprocess.run([sys.executable, band, ev, o], check=True, stdout=subprocess.DEVNULL)

def load(p):
    rows = [l.rstrip('\n').split('\t') for l in open(p)]
    hdr = rows[0]
    if hdr[0] != 'record': hdr = ['record', 'kind'] + hdr
    return {r[0]: dict(zip(hdr, r)) for r in rows[1:] if r and r[0].isdigit()}
B, A, K = load(out + '.before.tsv'), load(out + '.after.tsv'), load(b57)

def frames_equal(kind, rec):
    fb = sorted(glob.glob(f'{eb}/frames/{kind}_r{rec}/frame_*.png'))
    fa = sorted(glob.glob(f'{ea}/frames/{kind}_r{rec}/frame_*.png'))
    if not fb or len(fb) != len(fa): return f'no ({len(fb)} vs {len(fa)} frames)'
    h = lambda p: hashlib.sha256(open(p, 'rb').read()).hexdigest()
    diff = [os.path.basename(x) for x, y in zip(fb, fa) if h(x) != h(y)]
    return 'yes' if not diff else f'no ({len(diff)}/{len(fb)} differ, first {diff[0]})'

def num(x):
    try:
        v = float(x); return None if math.isnan(v) else v
    except (TypeError, ValueError):
        return None

AX = ['landing_cov', 'landing_end_s', 'p90_land', 'source_cov']
hdr = ['record', 'kind', 'frames_identical']
for a in AX:
    hdr += [f'oracle_{a}', f'b1157_{a}', f'before_{a}', f'after_{a}', f'd_{a}', f'vb_{a}', f'va_{a}']
hdr += ['moved_axes']
lines = ['\t'.join(hdr)]
for rec in sorted(A, key=int):
    a, b, k = A[rec], B.get(rec, {}), K.get(rec, {})
    kind = a['kind']
    row = [rec, kind, frames_equal(kind, rec)]
    moved = []
    for ax in AX:
        o, kb, vb, va = a.get(f'oracle_{ax}', ''), k.get(f'harness_{ax}', ''), b.get(f'harness_{ax}', ''), a.get(f'harness_{ax}', '')
        nb, na = num(vb), num(va)
        d = f'{na - nb:+.3f}' if nb is not None and na is not None else ('' if vb == va else 'n/a')
        if d not in ('', '+0.000', '-0.000'): moved.append(ax)
        row += [o, kb, vb, va, d, b.get(f'verdict_{ax}', ''), a.get(f'verdict_{ax}', '')]
    row.append(','.join(moved) if moved else '-')
    lines.append('\t'.join(row))
open(out, 'w').write('\n'.join(lines) + '\n')
print('\n'.join(lines))
