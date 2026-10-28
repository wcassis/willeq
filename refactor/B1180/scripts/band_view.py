#!/usr/bin/env python3
"""B1180: view my gate's b1130_r<rec> measure dirs under band_table.py's
<kind>_r<rec> names (B1155's layout), then run band_table.py over the view.
usage: band_view.py <gate_after_dir> <worktree> <outdir>
"""
import os, subprocess, sys
EV, WT, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
TABLE = '/mnt/projects/fable/review/B1130/record_table.tsv'
view = os.path.join(OUT, 'band_view', 'measure')
os.makedirs(view, exist_ok=True)
hdr = None
for line in open(TABLE):
    c = line.rstrip('\n').split('\t')
    if c[0] == 'record':
        hdr = c
        continue
    if c[0] == '51':
        continue
    kind = 'fixed_npc' if 'fixed_npc' in c[11] else 'fixed_self'
    link = os.path.join(view, f'{kind}_r{c[0]}')
    tgt = os.path.join(EV, 'measure', f'b1130_r{c[0]}')
    if os.path.exists(tgt) and not os.path.exists(link):
        os.symlink(tgt, link)
subprocess.run([sys.executable, os.path.join(WT, 'refactor/B1155/scripts/band_table.py'),
                os.path.join(OUT, 'band_view'), os.path.join(OUT, 'b1130_band.tsv')], check=True)
print(open(os.path.join(OUT, 'b1130_band.tsv')).read())
