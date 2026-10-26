#!/usr/bin/env python3
"""B1155: the per-preset median harness/oracle ratios, from b1130_rows.tsv.

usage: medians.py <evidence_dir>

The ONE command the README quotes for its median table (finding 2 of the
56d86164 review: the stated medians did not regenerate). It reads the SAME
sources band_table.py reads -- review/B1130/record_table.tsv (the B1130
oracle) and <evidence_dir>/measure/<kind>_r<rec>/b1130_rows.tsv (the harness
measure, one row per record) -- and, for each of source_cov and landing_cov,
computes the median over the sweep's 40 records (record 51 excluded, as in
band_table.py) of (harness value / oracle value), grouped by preset
(fixed_npc / fixed_self / ALL).

A record contributes a ratio only when BOTH the oracle and harness values are
present and the oracle is nonzero; the median is over the records that do.
Prints a markdown table ready to paste into the README.
"""
import sys, os, re, statistics

EV = sys.argv[1]
TABLE = '/mnt/projects/fable/review/B1130/record_table.tsv'
METRICS = ['source_cov', 'landing_cov']


def f(x):
    try:
        return float(x)
    except (ValueError, TypeError):
        return None


# oracle: rec -> {kind, source_cov, landing_cov}
oracle = {}
for line in open(TABLE):
    c = line.rstrip('\n').split('\t')
    if c[0] == 'record':
        continue
    rec = int(c[0])
    oracle[rec] = dict(
        kind='fixed_npc' if 'fixed_npc' in c[11] else 'fixed_self',
        source_cov=f(c[16]),
        landing_cov=f(c[23]))

# harness: rec -> {source_cov, landing_cov}
harness = {}
for rec in sorted(oracle):
    if rec == 51:          # B1130's not-captured record (band_table.py skips it)
        continue
    tag = f"{oracle[rec]['kind']}_r{rec}"
    mroot = os.path.join(EV, 'measure', tag, 'b1130_rows.tsv')
    if not os.path.exists(mroot):
        continue
    # the harness cast the EQ spell id; match on it, as band_table.py does.
    o_spell = None
    for line in open(TABLE):
        c = line.rstrip('\n').split('\t')
        if c[0] == 'record':
            continue
        if int(c[0]) == rec:
            o_spell = int(c[5])
            break
    h = {}
    if o_spell is not None:
        for line in open(mroot):
            c = line.rstrip('\n').split('\t')
            if not c[0].isdigit():
                continue
            if int(c[0]) == o_spell:
                h = dict(source_cov=f(c[2]), landing_cov=f(c[8]))
    if h:
        harness[rec] = h


def med(ratios):
    return statistics.median(ratios) if ratios else None


groups = {'ALL': [], 'fixed_npc': [], 'fixed_self': []}
for rec, h in harness.items():
    o = oracle[rec]
    for m in METRICS:
        ov, hv = o[m], h[m]
        if ov is None or hv is None or ov == 0:
            continue
        r = hv / ov
        groups['ALL'].append((m, r))
        groups[o['kind']].append((m, r))

lines = ['| | ' + ' | '.join(METRICS) + ' |', '|---|---|---|']
for g in ('ALL', 'fixed_npc', 'fixed_self'):
    row = [g]
    for m in METRICS:
        rs = [r for mm, r in groups[g] if mm == m]
        row.append(f'**{med(rs):.3f}**' if g == 'ALL' else f'{med(rs):.3f}' if rs else '-')
    lines.append('| ' + ' | '.join(row) + ' |')

n = len(harness)
print(f"# median harness/oracle ratios over {n} records (record 51 excluded)")
print('\n'.join(lines))
