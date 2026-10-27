#!/usr/bin/env python3
"""B1164: rows/cols of the non-background pixels of a willeq harness frame (uniform clear colour,
taken at pixel (5,5)): max-RGB difference > thr. On a pre-cast b1118 frame the union of the
caster and the target standing on it; the top row is the taller figure's head.
usage: b1164_silhouette.py <frame.png> [thr=30] [x0 x1]"""
import sys, numpy as np, cv2
a = cv2.imread(sys.argv[1]).astype(np.int16)
thr = int(sys.argv[2]) if len(sys.argv) > 2 else 30
x0, x1 = (int(sys.argv[3]), int(sys.argv[4])) if len(sys.argv) > 4 else (0, a.shape[1] - 1)
m = (np.abs(a - a[5, 5]).max(axis=2) > thr)[:, x0:x1 + 1]
ys, xs = np.where(m)
print(f'{sys.argv[1]} thr {thr} cols {x0}..{x1}: rows {ys.min()}..{ys.max()} ({ys.max() - ys.min() + 1} px), cols {x0 + xs.min()}..{x0 + xs.max()}')
