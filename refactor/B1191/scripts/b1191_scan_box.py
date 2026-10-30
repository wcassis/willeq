#!/usr/bin/env python3
"""Scan all frames in a box, find peak lit80 frame, report per-channel there.
usage: scan_box.py <fdir> <glob> <box> <tail_n> <fps>
box = 'r0:r1:c0:c1'
"""
import os, sys, glob
import numpy as np, cv2

fdir, g, box, tail_n, fps = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), float(sys.argv[5])
r0, r1, c0, c1 = [int(x) for x in box.split(':')]
files = sorted(glob.glob(os.path.join(fdir, g)), key=lambda p: int(''.join(ch for ch in os.path.basename(p) if ch.isdigit())))
def load(p): return cv2.imread(p, cv2.IMREAD_COLOR)[:, :, ::-1].astype(np.float32)
imgs = [load(p) for p in files]
base = np.median(np.stack([im for im in imgs[-tail_n:]]), axis=0)
best_i, best_n = 0, -1
per_frame = []
for i, img in enumerate(imgs):
    d = np.clip(img - base, 0, 255)[r0:r1, c0:c1]
    n = int((d.max(axis=2) > 80).sum())
    per_frame.append((i, n))
    if n > best_n: best_n, best_i = n, i
print(f'peak frame idx {best_i} (file {os.path.basename(files[best_i])}) t={best_i/fps:.2f}s lit80={best_n}')
d = np.clip(imgs[best_i] - base, 0, 255)[r0:r1, c0:c1]
mx = d.max(axis=2)
for name, thr in (('lit80',80),('px150',150)):
    m = mx>thr
    if m.sum()>50:
        mm=d[m].mean(axis=0)
        print(f'{name} n={int(m.sum())} R={mm[0]:.1f} G={mm[1]:.1f} B={mm[2]:.1f} G/R={mm[1]/mm[0]:.3f}')
    else:
        print(f'{name} n={int(m.sum())} (empty)')
# top 10 frames by lit80
top = sorted(per_frame, key=lambda x:-x[1])[:10]
print('top frames (idx lit80):', ' '.join(f'{i}:{n}' for i,n in top))
