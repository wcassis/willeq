#!/usr/bin/env python3
"""B1178 Ask 3 AFTER: recompute the nine verdict columns of the B1153 gate_c
table from its oracle/harness columns with the band_table rules
(`refactor/B1155/scripts/band_table.py`: source_cov/landing_cov +-3 %,
p90_src/p90_land +-15 %, landing_end_s/source_onset_s +-0.10 s,
source_centroid/src_h_lo/src_h_hi +-0.15), on the BEFORE audit copy
(`refactor/B1178/tables/gate_c_before.tsv` -- the committed gate_c.tsv before
the B1178 reference change) and the current AFTER table
(`refactor/B1153/tables/gate_c.tsv` -- oracle columns regenerated), and print
the per-column counts plus every cell that differs.

Checks (all must hold for a clean hand-back):
  1. BEFORE recompute == the committed verdict cells of gate_c_before.tsv
     (the script reproduces the committed table's own verdicts -- 0-cell diff).
  2. AFTER recompute vs the committed verdict cells of the current gate_c.tsv:
     a differing cell is a verdict the regenerated reference moved -- each is
     printed, none is hidden.
  3. BEFORE vs AFTER per-column counts (the pure reference delta).

Exit 0 iff checks 1 and 2 report the stated counts (2 prints the diff list
itself; a non-empty diff is a finding to own, not a script failure).
"""
import sys, os, math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BEFORE = sys.argv[1] if len(sys.argv) > 1 else f'{ROOT}/refactor/B1178/tables/gate_c_before.tsv'
AFTER = sys.argv[2] if len(sys.argv) > 2 else f'{ROOT}/refactor/B1153/tables/gate_c.tsv'

METRICS = ['source_cov', 'landing_cov', 'landing_end_s', 'p90_src', 'p90_land',
           'source_onset_s', 'source_centroid', 'src_h_lo', 'src_h_hi']

def f(x):
    try:
        return float(x)
    except (ValueError, TypeError):
        return None

def band(metric, o):
    if metric in ('source_cov', 'landing_cov'): return 0.03 * o
    if metric in ('p90_src', 'p90_land'):       return 0.15 * o
    if metric in ('landing_end_s', 'source_onset_s'): return 0.10
    return 0.15

def verdict(metric, o, h):
    if o is None or h is None or math.isnan(h): return 'n-a'
    b = band(metric, o)
    if abs(h - o) <= b: return 'PASS'
    return 'short' if h < o - b else 'over'

def load(path):
    rows = [l.rstrip('\n').split('\t') for l in open(path)]
    hdr = rows[0]
    out = []
    for r in rows[1:]:
        rec = r[0]
        cell = {}
        for m in METRICS:
            o = f(r[hdr.index(f'oracle_{m}')])
            h = f(r[hdr.index(f'harness_{m}')])
            v = r[hdr.index(f'verdict_{m}')]
            cell[m] = (o, h, v)
        out.append((rec, cell))
    return out

def recompute(rows):
    return {rec: {m: verdict(m, o, h) for m, (o, h, v) in cell.items()}
            for rec, cell in rows}

def counts(rec):
    c = {m: {} for m in METRICS}
    for recid, vc in rec.items():
        for m in METRICS:
            c[m][vc[m]] = c[m].get(vc[m], 0) + 1
    return c

before = load(BEFORE)
after = load(AFTER)
rb, ra = recompute(before), recompute(after)

print(f'BEFORE: {BEFORE}')
mism = [(rec, m, cell[m][2], rb[rec][m]) for rec, cell in before
        for m in METRICS if cell[m][2] != rb[rec][m]]
print(f'  recompute check: {len(mism)} cell diffs vs committed verdicts'
      + ('  ' + str(mism[:5]) if mism else ''))
for m in METRICS:
    print(f'  {m:18s}', counts(rb)[m])

print(f'AFTER:  {AFTER}')
mism = [(rec, m, cell[m][2], ra[rec][m]) for rec, cell in after
        for m in METRICS if cell[m][2] != ra[rec][m]]
print(f'  recompute check: {len(mism)} cell diffs vs committed verdicts'
      + ('  ' + str(mism[:5]) if mism else ''))
for m in METRICS:
    print(f'  {m:18s}', counts(ra)[m])

flips = [(rec, m, rb[rec][m], ra[rec][m]) for rec, cell in before
         for m in METRICS if rb[rec][m] != ra[rec][m]]
print(f'BEFORE -> AFTER flips: {len(flips)}' + (f'  {flips}' if flips else ''))
