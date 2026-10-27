#!/usr/bin/env python3
"""B1173 (#492): frame comparisons for the r7 diagnostic runs (run_r7_diag.sh).

usage: python3 -P compare_r7_diag.py <runA_frames> <runB_frames> <label> [col_limit] [f0] [f1]
Prints, for frames f0..f1 (default 0..509), the first frame whose pixels differ
anywhere and the first frame whose pixels differ left of col_limit (default 800),
plus the number of frames that differ in each region; one line per call."""
import sys, cv2, numpy as np
a, b, label = sys.argv[1:4]
col = int(sys.argv[4]) if len(sys.argv) > 4 else 800
f0 = int(sys.argv[5]) if len(sys.argv) > 5 else 0
f1 = int(sys.argv[6]) if len(sys.argv) > 6 else 509
first_any = first_left = None; n_any = n_left = 0; left_px = {}
for f in range(f0, f1 + 1):
    x = cv2.imread(f'{a}/frame_{f:05d}.png'); y = cv2.imread(f'{b}/frame_{f:05d}.png')
    if x is None or y is None: break
    d = np.abs(x.astype(int) - y.astype(int)).max(2) > 0
    if d.any():
        n_any += 1; first_any = f if first_any is None else first_any
    l = int(d[:, :col].sum())
    if l:
        n_left += 1; left_px[f] = l
        first_left = f if first_left is None else first_left
fl = f'{first_left} ({left_px[first_left]} px)' if first_left is not None else 'none'
print(f'{label}\tframes {f0}..{f}\tdiffer anywhere {n_any} (first {first_any})\t'
      f'differ left of col {col} {n_left} (first {fl})')
