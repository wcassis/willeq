#!/usr/bin/env python3
"""B1169 (#488): per-frame pixel diff of two FX-harness frame dirs (same frame names).
The "identical" proof of tools/model_viewer/README.md "Determinism": 0 differing frames.
usage: python3 -P fx_frame_identity.py <dirA> <dirB> [--per-frame out.tsv]
Prints one line: frames compared, frames differing, first differing frame, its
differing-pixel count, bbox (rows/cols) and max channel delta; plus the frame with
the largest differing area. Every number is read off the PNGs (cv2)."""
import sys, os, glob
import cv2, numpy as np
a, b = sys.argv[1], sys.argv[2]
per = sys.argv[sys.argv.index('--per-frame') + 1] if '--per-frame' in sys.argv else None
fa = sorted(os.path.basename(p) for p in glob.glob(os.path.join(a, 'frame_*.png')))
fb = set(os.path.basename(p) for p in glob.glob(os.path.join(b, 'frame_*.png')))
rows = []
for n in fa:
    if n not in fb:
        rows.append((n, -1, '', 0)); continue
    x = cv2.imread(os.path.join(a, n), cv2.IMREAD_UNCHANGED)
    y = cv2.imread(os.path.join(b, n), cv2.IMREAD_UNCHANGED)
    if x.shape != y.shape:
        rows.append((n, -2, 'shape', 0)); continue
    d = np.abs(x.astype(np.int16) - y.astype(np.int16))
    d = d.max(axis=2) if d.ndim == 3 else d
    k = int((d > 0).sum())
    if k:
        ys, xs = np.nonzero(d)
        rows.append((n, k, f'rows {ys.min()}-{ys.max()} cols {xs.min()}-{xs.max()}', int(d.max())))
    else:
        rows.append((n, 0, '', 0))
diff = [r for r in rows if r[1] != 0]
first = diff[0] if diff else None
big = max(diff, key=lambda r: r[1]) if diff else None
fmt = lambda r: f'{r[0]} px {r[1]} {r[2]} max {r[3]}' if r else '-'
print(f'{a} vs {b}: frames {len(fa)}/{len(fb)} differing {len(diff)} first [{fmt(first)}] largest [{fmt(big)}]')
if per:
    with open(per, 'w') as o:
        o.write('frame\tpx\tbbox\tmax\n')
        for r in rows:
            o.write(f'{r[0]}\t{r[1]}\t{r[2]}\t{r[3]}\n')
sys.exit(1 if diff else 0)
