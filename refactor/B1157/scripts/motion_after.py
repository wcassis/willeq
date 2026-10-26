#!/usr/bin/env python3
"""B1157: run B1151's motion_table.py against this sweep's anchor rows.

motion_table.py hard-codes the b1151_r<rec>_<spell> row names; this wraps it
with a symlink farm so the SAME instrument (same verdict rules) reads the
B1157 runs, then the two tables (B1151's committed SOURCE table = the
pre-B1157 idle caster; this one = the animated caster) are diffed row by row
-- the 18 unattributed B1151 Source rows are what this card explains.

usage: motion_after.py <ev_b1118_dir> <b1151_runs_dir> <b1151_oracle_dir> <out.tsv>
"""
import sys, os, glob, re, shutil, subprocess

EV, B1151RUNS, ORACLE, out = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
farm = out + '.symlinks'
if os.path.exists(farm):
    shutil.rmtree(farm)
os.makedirs(farm)
n = 0
for d in sorted(glob.glob(os.path.join(EV, 'measure', 'b1157_r*'))):
    m = re.match(r'b1157_r(\d+)_(\d+)$', os.path.basename(d))
    if m and os.path.exists(os.path.join(d, 'anchor_SOURCE.tsv')):
        os.symlink(d, os.path.join(farm, f'b1151_r{m.group(1)}_{m.group(2)}'))
        n += 1
print(f'{n} rows linked -> {farm}')
with open(out, 'w') as fh:
    subprocess.run([sys.executable,
                    os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'B1151', 'scripts', 'motion_table.py'),
                    farm, ORACLE], check=False, stdout=fh)
print(f'motion_table.py (the B1151 instrument) wrote {out} (rows: {n})')
