#!/usr/bin/env python3
"""B1171 (#490): the b1130 40-record sweep, before (fable, the table's class level)
vs after (this branch, the capture's `#level`), vs the oracle (review/B1130/record_table.tsv
through B1155's band_table.py), vs B1157's committed banded table
(refactor/B1157/b1130_table.tsv.after.tsv) and vs B1166's committed table
(refactor/B1166/tables/b1130_table.tsv, its `after_*` columns).

Every row carries the oracle capture's level (scripts/fx/capture_level.py: the capture's
plan.tsv + executed steps.tsv) and each run's harness level (its timing.tsv
caster_level column, else its plan). A verdict whose harness level differs from the
oracle's is REFUSED (capture_level.verdict_or_refusal): the before verdicts are, which
is the defect; an after row refused is an error (exit 3).

usage: compare_b1130.py <ev_before> <ev_after> <out.tsv> [owners.tsv]
  owners.tsv (optional): record<TAB>owner, joined as the last column.
Writes <out>.before.tsv / <out>.after.tsv (band_table.py's own output) and <out>.
"""
import glob
import hashlib
import math
import os
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.abspath(os.path.join(here, '..', '..', '..'))
sys.path.insert(0, os.path.join(repo, 'scripts', 'fx'))
import capture_level as CL  # noqa: E402

eb, ea, out = sys.argv[1:4]
owners = {}
if len(sys.argv) > 4:
    for line in open(sys.argv[4]):
        if line.strip() and not line.startswith('#'):
            r, o = line.rstrip('\n').split('\t', 1)
            owners[r] = o
band = os.path.join(repo, 'refactor', 'B1155', 'scripts', 'band_table.py')
b57 = os.path.join(repo, 'refactor', 'B1157', 'b1130_table.tsv.after.tsv')
b66 = os.path.join(repo, 'refactor', 'B1166', 'tables', 'b1130_table.tsv')
for ev, o in ((eb, out + '.before.tsv'), (ea, out + '.after.tsv')):
    subprocess.run([sys.executable, band, ev, o], check=True, stdout=subprocess.DEVNULL)


def load(p):
    rows = [l.rstrip('\n').split('\t') for l in open(p)]
    hdr = rows[0]
    if hdr[0] != 'record':
        hdr = ['record', 'kind'] + hdr
    return {r[0]: dict(zip(hdr, r)) for r in rows[1:] if r and r[0].isdigit()}


B, A, K, S = load(out + '.before.tsv'), load(out + '.after.tsv'), load(b57), load(b66)


def frames_equal(kind, rec):
    fb = sorted(glob.glob(f'{eb}/frames/{kind}_r{rec}/frame_*.png'))
    fa = sorted(glob.glob(f'{ea}/frames/{kind}_r{rec}/frame_*.png'))
    if not fb or len(fb) != len(fa):
        return f'no ({len(fb)} vs {len(fa)} frames)'
    h = lambda p: hashlib.sha256(open(p, 'rb').read()).hexdigest()  # noqa: E731
    diff = [os.path.basename(x) for x, y in zip(fb, fa) if h(x) != h(y)]
    return 'yes' if not diff else f'no ({len(diff)}/{len(fb)} differ, first {diff[0]})'


def num(x):
    try:
        v = float(x)
        return None if math.isnan(v) else v
    except (TypeError, ValueError):
        return None


def run_level(ev, kind, rec):
    try:
        lv, _src = CL.harness_level(f'{ev}/frames/{kind}_r{rec}', f'{ev}/plans/r{rec}.plan')
    except (CL.LevelError, OSError):
        return None  # no finished run: unknown, so refused
    return lv


AX = ['landing_cov', 'landing_end_s', 'p90_land', 'source_cov']
hdr = ['record', 'kind', 'capture', 'oracle_level', 'before_level', 'after_level', 'frames_identical']
for a in AX:
    hdr += [f'oracle_{a}', f'b1157_{a}', f'b1166_{a}', f'before_{a}', f'after_{a}', f'd_{a}',
            f'vb_{a}', f'va_{a}']
hdr += ['moved_axes', 'verdict_changes', 'owner']
lines = ['\t'.join(hdr)]
refused_after = []
for rec in sorted(A, key=int):
    a, b, k, s = A[rec], B.get(rec, {}), K.get(rec, {}), S.get(rec, {})
    kind = a['kind']
    olv = CL.b1130_record_level(rec)
    blv, alv = run_level(eb, kind, rec), run_level(ea, kind, rec)
    try:
        CL.refuse(olv, alv)
    except CL.LevelMismatch as e:
        refused_after.append(f'r{rec}: {e}')
    row = [rec, kind, CL.b1130_record_capture(rec), str(olv), str(blv), str(alv), frames_equal(kind, rec)]
    moved, changes = [], []
    for ax in AX:
        o, kb, sb = a.get(f'oracle_{ax}', ''), k.get(f'harness_{ax}', ''), s.get(f'after_{ax}', '')
        vb, va = b.get(f'harness_{ax}', ''), a.get(f'harness_{ax}', '')
        nb, na = num(vb), num(va)
        d = f'{na - nb:+.3f}' if nb is not None and na is not None else ('' if vb == va else 'n/a')
        if d not in ('', '+0.000', '-0.000'):
            moved.append(ax)
        vrb = CL.verdict_or_refusal(olv, blv, b.get(f'verdict_{ax}', ''))
        vra = CL.verdict_or_refusal(olv, alv, a.get(f'verdict_{ax}', ''))
        # the verdict the before run WOULD have printed, vs the after's
        vb_raw = b.get(f'verdict_{ax}', '')
        if vb_raw != vra:
            changes.append(f'{ax}:{vb_raw}->{vra}')
        row += [o, kb, sb, vb, va, d, vrb, vra]
    row += [','.join(moved) if moved else '-', ','.join(changes) if changes else '-', owners.get(rec, '')]
    lines.append('\t'.join(row))
open(out, 'w').write('\n'.join(lines) + '\n')
print('\n'.join(lines))
if refused_after:
    print('REFUSED after rows:\n  ' + '\n  '.join(refused_after), file=sys.stderr)
    sys.exit(3)
