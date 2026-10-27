#!/usr/bin/env bash
# B1167: the before/after gate tables -> refactor/B1167/tables/ (run from the worktree root).
#   b1130_table.tsv  B1166's compare_b1130.py (B1155's band_table.py verdicts vs review/B1130, B1157 column)
#   b1118_table.tsv  compare_b1118.py (B1157's _n columns, B1142 lane-5 for the tour)
#   summary.txt      toward / away from the oracle per b1130 axis, verdict changes, moved b1118 rows
set -eu
EV=/mnt/projects/fable/review/B1167/worker; T=refactor/B1167/tables; mkdir -p $T
python3 -P refactor/B1166/scripts/compare_b1130.py $EV/sweeps_before $EV/sweeps_after $T/b1130_table.tsv > /dev/null
python3 -I refactor/B1167/scripts/compare_b1118.py $EV/sweeps_before $EV/sweeps_after $T/b1118_table.tsv > /dev/null
python3 -I - "$T" > $T/summary.txt <<'PY'
import sys, csv
T = sys.argv[1]
rows = list(csv.DictReader(open(f'{T}/b1130_table.tsv'), delimiter='\t'))
def f(x):
    try: return float(x)
    except Exception: return None
print('# b1130 (level 60): per axis, rows whose after is nearer / farther from the oracle than before (|d| > 0.001)')
for ax in ('landing_cov', 'landing_end_s', 'p90_land', 'source_cov'):
    near = far = same = 0; ch = []
    for r in rows:
        o, b, a = f(r[f'oracle_{ax}']), f(r[f'before_{ax}']), f(r[f'after_{ax}'])
        if r[f'vb_{ax}'] != r[f'va_{ax}']: ch.append(f"r{r['record']} {r[f'vb_{ax}']}->{r[f'va_{ax}']}")
        if None in (o, b, a) or abs(a - b) <= 0.001: same += 1; continue
        if abs(a - o) < abs(b - o): near += 1
        else: far += 1
    pb = sum(r[f'vb_{ax}'] == 'PASS' for r in rows); pa = sum(r[f'va_{ax}'] == 'PASS' for r in rows)
    print(f'{ax}: nearer {near}, farther {far}, unchanged/n-a {same}; PASS {pb} -> {pa}; verdict changes: {", ".join(ch) or "none"}')
ident = sum(r['frames_identical'] == 'yes' for r in rows)
print(f'frames identical before/after: {ident} of {len(rows)}')
r2 = list(csv.DictReader(open(f'{T}/b1118_table.tsv'), delimiter='\t'))
mv = [r for r in r2 if r['moved'] != '-']
print(f'# b1118: {len(mv)} of {len(r2)} rows moved (land > 3 %, end > 0.10 s, src > 3 %); frames identical {sum(r["frames_identical"]=="yes" for r in r2)}')
for r in r2:
    if r['row'].startswith('tour'):
        print(f"{r['row']}: land {r['land_before']} -> {r['land_after']} (lane5 {r['lane5_land']}), end {r['end_before']} -> {r['end_after']} (lane5 {r['lane5_end']}), src {r['src_before']} -> {r['src_after']}, moved {r['moved']}")
PY
cat $T/summary.txt
