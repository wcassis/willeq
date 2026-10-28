#!/usr/bin/env python3
"""B1172 fix leg (review note 7955 finding 4): the b1130 base drift, before vs B1157, split by
GL context. For each of the 40 records: B1157's committed value (refactor/B1157/
b1130_table.tsv.after.tsv, via refactor/B1172/tables/b1130_table.tsv's b1157_* columns), the
B1172 before value (fable 070a9215, NVIDIA surfaceless, the same table's before_* columns) and
the same before binary re-rendered on the ThinkPad's Intel GPU through DRM/GBM -- the context
B1157's run_sweep_b1130.sh used (rig_check.sh -> review/B1172/worker/fix/rig/measure/
before_<kind>_r<rec>_i915/b1130_rows.tsv), and B1166's own after value
(refactor/B1166/tables/b1130_table.tsv after_*: B1166's tip, rendered by its run_sweep_b1130.sh,
which also has no DRM override = the Intel context). Values compared at 3 decimals; nan / empty =
no reading.
usage: compare_rig.py <b1130_table.tsv> <rig_measure_dir> <b1166_table.tsv> <out.tsv>"""
import csv, math, os, sys

table, rig, t66, out = sys.argv[1:5]
AX = [('landing_cov', 'landing_cov'), ('landing_end_s', 'landing_end_s'),
      ('p90_land', 'p90_land'), ('source_cov', 'source_cov')]


def num(x):
    try:
        v = float(x)
        return None if math.isnan(v) else round(v, 3)
    except (TypeError, ValueError):
        return None


rows = list(csv.DictReader(open(table), delimiter='\t'))
B66 = {r['record']: r for r in csv.DictReader(open(t66), delimiter='\t')}
hdr = ['record', 'kind']
for a, _ in AX:
    hdr += [f'b1157_{a}', f'b1166after_{a}', f'before_nvidia_{a}', f'before_i915_{a}']
hdr += ['cells_b1157_eq_nvidia', 'cells_b1157_eq_i915', 'cells_b1166after_eq_i915', 'i915_rendered']
lines = ['\t'.join(hdr)]
tot = {'n': 0, 'eq_nv': 0, 'eq_i9': 0}
per_kind = {}
for r in rows:
    kind = r['kind']
    short = kind.replace('fixed_', '')
    p = os.path.join(rig, f'before_{short}_r{r["record"]}_i915', 'b1130_rows.tsv')
    i9 = next(csv.DictReader(open(p), delimiter='\t')) if os.path.exists(p) else None
    row = [r['record'], kind]
    eq_nv = eq_i9 = eq66 = 0
    r66 = B66.get(r['record'], {})
    for a, col in AX:
        b, nv = num(r[f'b1157_{a}']), num(r[f'before_{a}'])
        iv = num(i9[col]) if i9 else None
        v66 = num(r66.get(f'after_{a}'))
        row += [r[f'b1157_{a}'], r66.get(f'after_{a}', ''), r[f'before_{a}'], (i9[col] if i9 else '')]
        eq66 += (v66 == iv) if i9 else 0
        eq_nv += (b == nv)
        eq_i9 += (b == iv) if i9 else 0
    row += [str(eq_nv), str(eq_i9) if i9 else '', str(eq66) if i9 else '', 'yes' if i9 else 'no']
    lines.append('\t'.join(row))
    if i9:
        k = per_kind.setdefault(kind, {'records': 0, 'cells': 0, 'eq_nv': 0, 'eq_i9': 0, 'all4_i9': 0, 'eq66': 0, 'all4_66': 0})
        k['records'] += 1; k['cells'] += 4; k['eq_nv'] += eq_nv; k['eq_i9'] += eq_i9
        k['all4_i9'] += (eq_i9 == 4); k['eq66'] += eq66; k['all4_66'] += (eq66 == 4)
open(out, 'w').write('\n'.join(lines) + '\n')
for kind, k in sorted(per_kind.items()):
    print(f"{kind}: {k['records']} records rendered on i915, {k['cells']} cells; equal to B1157: "
          f"NVIDIA before {k['eq_nv']}, i915 before {k['eq_i9']}; records with all 4 cells equal on i915: {k['all4_i9']}; "
          f"i915 before equal to B1166's after: {k['eq66']} cells, {k['all4_66']} records all 4")
