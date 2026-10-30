#!/usr/bin/env python3
"""Same-frame whole-frame per-channel for an A/B pair of frame dirs.
usage: ab_pair.py <etcl_dir> <rgba_dir> <glob> <fps> <tail_n> <frame_idx>
Prints per-channel px150/lit80 means for BOTH at the given frame (0-based idx).
"""
import os, sys, glob
import numpy as np, cv2

etcl_dir, rgba_dir, g, fps, tail_n, fidx = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
def files(d): return sorted(glob.glob(os.path.join(d, g)), key=lambda p: int(''.join(c for c in os.path.basename(p) if c.isdigit())))
def load(p): return cv2.imread(p, cv2.IMREAD_COLOR)[:, :, ::-1].astype(np.float32)

for label, d in (('ETC1', etcl_dir), ('RGBA', rgba_dir)):
    fs = files(d)
    base = np.median(np.stack([load(p) for p in fs[-tail_n:]]), axis=0)
    img = load(fs[fidx])
    diff = np.clip(img - base, 0, 255)
    mx = diff.max(axis=2)
    for name, thr in (('lit80', 80), ('px150', 150)):
        m = mx > thr
        if m.sum() > 50:
            mm = diff[m].mean(axis=0)
            print(f'{label} f{fidx} {name}: px={int(m.sum())} R={mm[0]:.1f} G={mm[1]:.1f} B={mm[2]:.1f}  G/R={mm[1]/mm[0]:.3f}')
        else:
            print(f'{label} f{fidx} {name}: px={int(m.sum())} (empty)')
