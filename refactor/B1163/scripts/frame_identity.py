#!/usr/bin/env python3
"""B1163 (#482) rework: frame identity between two harness runs of one plan.

Usage: python3 frame_identity.py <run_dir_a> <run_dir_b> [...pairs]
Prints, per pair: frames in each, frames whose PNG bytes differ, and for those
the largest number of differing pixels in one frame and the bounding box
(rows/cols) of every differing pixel. Needs numpy + cv2 (dev-u24: plain
python3, not -I)."""
import glob, hashlib, os, sys
import cv2
import numpy as np

def md5(p):
    return hashlib.md5(open(p, 'rb').read()).hexdigest()

args = sys.argv[1:]
for a, b in zip(args[0::2], args[1::2]):
    fa = sorted(glob.glob(os.path.join(a, '*.png')))
    fb = {os.path.basename(p): p for p in glob.glob(os.path.join(b, '*.png'))}
    diff, mx, worst, rows, cols = 0, 0, '-', [], []
    for p in fa:
        q = fb.get(os.path.basename(p))
        if q is None or md5(p) == md5(q):
            continue
        diff += 1
        d = np.abs(cv2.imread(p).astype(int) - cv2.imread(q).astype(int)).max(axis=2)
        ij = np.argwhere(d > 0)
        if len(ij):
            rows += [int(ij[:, 0].min()), int(ij[:, 0].max())]
            cols += [int(ij[:, 1].min()), int(ij[:, 1].max())]
            if len(ij) > mx:
                mx, worst = len(ij), os.path.basename(p)
    box = f"rows {min(rows)}-{max(rows)} cols {min(cols)}-{max(cols)}" if rows else "-"
    print(f"{a} vs {b}: frames {len(fa)}/{len(fb)} differing {diff} "
          f"max_px {mx} ({worst}) {box}")
