#!/usr/bin/env python3
# B1195: pretty-print lane_table.tsv: per cast (spell rep), per metric: lane5 / lane2 / delta / verdict.
import sys
rows = [l.rstrip('\n').split('\t') for l in open(sys.argv[1])]
hdr = rows[0]
metrics = [h[5:] for h in hdr if h.startswith('lane5_')][:13] if hdr[3].startswith('lane5') else None
# rebuild metric list from header order
metrics = []
for h in hdr:
    if h.startswith('lane5_'):
        metrics.append(h[len('lane5_'):])
for r in rows[1:]:
    cast = ' '.join(r[:3])
    print(f'== {cast} ==')
    for i, m in enumerate(metrics):
        v5, v2, d, v = r[3 + i], r[3 + len(metrics) + i], r[3 + 2 * len(metrics) + i], r[3 + 3 * len(metrics) + i]
        print(f'  {m:20s} lane5={v5:>8s} lane2={v2:>8s} delta={d:>9s} {v}')
