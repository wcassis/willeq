#!/usr/bin/env python3
"""B1157: regenerate the b1118 record summary.tsv files with FX_SPELLS=auto.

b1123_summary.py (the summary table writer inside harness_measure.py) defaults
to the B1118 TOUR set [93 38 54 656 41 253 458]; the 40-record rows need
FX_SPELLS=auto (its own documented mode for the sweep) to list each record's
own spell. The fable_<spell> dirs, roi.txt and measure_*.txt are all kept in
the measure root (FX_KEEP=1), so the table is rebuilt from saved artifacts --
no re-render, no re-ffmpeg.
"""
import os, glob, subprocess, sys

EV = sys.argv[1] if len(sys.argv) > 1 else '/home/user/kb-evidence/kb-b1157-cast-anim/b1118'
S = '/mnt/projects/fable/review/B1132/scripts'
PY = '/home/user/.venvs/b1155/bin/python3'
n = 0
for d in sorted(glob.glob(os.path.join(EV, 'measure', 'b1157_r*'))):
    fables = [p for p in glob.glob(os.path.join(d, 'fable_*')) if os.path.isdir(p)]
    if not fables:
        continue
    out = subprocess.run([PY, os.path.join(S, 'b1123_summary.py'), d],
                         env={**os.environ, 'FX_SPELLS': 'auto',
                              'PATH': '/home/user/.venvs/b1155/bin:' + os.environ['PATH']},
                         capture_output=True, text=True)
    if out.returncode != 0 or 'missing' in out.stdout:
        print(f'FAIL {os.path.basename(d)}: rc={out.returncode}\n{out.stdout}\n{out.stderr[:400]}')
        continue
    with open(os.path.join(d, 'summary.tsv'), 'w') as f:
        f.write(out.stdout)
    n += 1
    print(f'ok {os.path.basename(d)}: {len(fables)} spell rows')
print(f'{n} summaries regenerated')
