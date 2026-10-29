#!/usr/bin/env python3
"""B1188 FIX (MR !342 note 8515, finding 2): the flat-gate claim, re-derived
from two COMMITTED tables.

The before-side of the B1188 delta is the merge base ac8c26bc6 (the B1186 tip
-- this branch's first parent, the same tree without the B1188 atlas change),
rendered with /mnt/projects/fable/review/GATE-BASELINE/render_gate.sh (PACK
pointed at the OLD pack rebuilt from the ac8c26bc worktree,
sha256 1cdd31e74594af36d667876e944cd1538fc2ffe6586de158f4b5c78fffded730)
and judged by build_tables_b1188.py with
  B1188_MEASURE=/mnt/scratch/kb-evidence/kb-b1188-landing-pixels/gate_base/measure
  B1188_OUT=refactor/B1188/tables/base
The after-side is the committed B1188 gate (refactor/B1188/tables/).

This script compares the two committed b1130 tables row by row over the nine
measured columns and prints, per column: the max |delta| (absolute and
relative to the base value) and the row that carries it, plus the row-0
(spell 93) headline values. The README's "flat" claim must quote what this
prints, not a remembered number.

Usage (from the MR's worktree, after both tables are committed):
  python3 -I refactor/B1188/scripts/delta_b1188_base.py
"""
import os
import sys

WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
BASE = os.path.join(WT, 'refactor/B1188/tables/base/b1130.tsv')
AFTER = os.path.join(WT, 'refactor/B1188/tables/b1130.tsv')

METRICS = ['source_cov', 'landing_cov', 'landing_end_s', 'p90_src', 'p90_land',
           'source_onset_s', 'source_centroid', 'src_h_lo', 'src_h_hi']


def f(x):
    x = (x or '').strip()
    try:
        return float(x)
    except ValueError:
        return None


def readtsv(p):
    with open(p) as fh:
        lines = [l.rstrip('\n') for l in fh if l.strip() and not l.lstrip().startswith('#')]
    hdr = lines[0].split('\t')
    return [dict(zip(hdr, l.split('\t'))) for l in lines[1:]]


TOUR = 'tour.tsv'
TOUR_METRICS = ['mine_land_cov', 'mine_end', 'mine_roi_l', 'mine_roi_s',
                'mine_src_cov', 'mine_src_on25', 'mine_src_cen']


def tourtable(p):
    with open(p) as fh:
        lines = [l.rstrip('\n') for l in fh if l.strip() and not l.lstrip().startswith('#')]
    hdr = lines[0].split('\t')
    return [dict(zip(hdr, l.split('\t'))) for l in lines[1:]]


def main():
    base = {int(r['record']): r for r in readtsv(BASE)}
    after = {int(r['record']): r for r in readtsv(AFTER)}
    recs = sorted(set(base) & set(after))
    print(f'rows compared: {len(recs)} (base {len(base)}, after {len(after)})')
    print(f'{"column":18s}{"max|delta|":>12s}  (row)   maxrel% (row)')
    for m in METRICS:
        # worst ABSOLUTE delta (what the README quotes as max|delta|) and the
        # worst RELATIVE delta are tracked separately (review note 8559 f1:
        # the old loop picked the row by relative delta and printed its
        # absolute delta as the max).
        worst, worst_row, worst_rel, worst_rel_row = None, None, -1.0, None
        for r in recs:
            b = f(base[r].get('mine_' + m))
            a = f(after[r].get('mine_' + m))
            if b is None or a is None or b == 0:
                continue
            d = abs(a - b)
            rel = d / abs(b) * 100.0
            if worst is None or d > worst:
                worst, worst_row = d, r
            if rel > worst_rel:
                worst_rel, worst_rel_row = rel, r
        print(f'{m:18s}{worst:12.3f} (r{worst_row})  maxrel {worst_rel:6.2f}% (r{worst_rel_row})')
    b0, a0 = base[0], after[0]
    o, lb, la = (f(base[0].get('oracle_landing_cov')),
                 f(base[0].get('mine_landing_cov')), f(after[0].get('mine_landing_cov')))
    if o and lb is not None and la is not None and o:
        print(f'row 0 (spell 93): base {lb} / after {la} / oracle {o} -> '
              f'{lb / o:.3f}x -> {la / o:.3f}x')

    tb = {int(r['spell']): r for r in tourtable(os.path.join(WT, 'refactor/B1188/tables/base', TOUR))}
    ta = {int(r['spell']): r for r in tourtable(os.path.join(WT, 'refactor/B1188/tables', TOUR))}
    print('\ntour-7 (spell 93/38/41/656/458/253/54):')
    for m in TOUR_METRICS:
        deltas = []
        for s in sorted(set(tb) & set(ta)):
            b, a = f(tb[s].get(m)), f(ta[s].get(m))
            if b is not None and a is not None:
                deltas.append((s, a - b))
        if not deltas:
            print(f'{m:16s} (n-a)')
            continue
        worst = max(deltas, key=lambda x: abs(x[1]))
        print(f'{m:16s} max|delta|={abs(worst[1]):.3f} (spell {worst[0]}); '
              f'all zero = {all(d == 0 for _, d in deltas)}')


if __name__ == '__main__':
    sys.exit(main())
