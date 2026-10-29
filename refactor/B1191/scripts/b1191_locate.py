#!/usr/bin/env python3
"""Locate the bright set: bbox + centroid of px>150 and lit80 at the peak frame, whole frame.
Also reports per-channel means in a data-driven box (bbox + 10% pad) for both sets.
usage: locate.py <framesdir> <glob> <fps> <tail_n>
"""
import os, sys, glob
import numpy as np, cv2

fdir, g, fps, tail_n = sys.argv[1], sys.argv[2], float(sys.argv[3]), int(sys.argv[4])
files = sorted(glob.glob(os.path.join(fdir, g)), key=lambda p: int(''.join(ch for ch in os.path.basename(p) if ch.isdigit())))
def load(p):
    return cv2.imread(p, cv2.IMREAD_COLOR)[:, :, ::-1].astype(np.float32)
frames = [load(p) for p in files]
base = np.median(np.stack([f for f in frames[-tail_n:]]), axis=0)
best_i, best_n = 0, -1
for i, img in enumerate(frames):
    d = np.clip(img - base, 0, 255)
    n = int((d.max(axis=2) > 80).sum())
    if n > best_n:
        best_n, best_i = n, i
img = frames[best_i]
d = np.clip(img - base, 0, 255)
mx = d.max(axis=2)
lit = mx > 80
px = mx > 150
print(f'peak frame {best_i} t={best_i/fps:.2f}s lit80 n={lit.sum()} px150 n={px.sum()}')
H, W = mx.shape
for name, mask in (('lit80', lit), ('px150', px)):
    ys, xs = np.where(mask)
    if len(xs) == 0:
        print(f'{name}: empty'); continue
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    print(f'{name}: bbox x[{x0}..{x1}] y[{y0}..{y1}]  centroid=({xs.mean():.0f},{ys.mean():.0f})')
# data-driven box from lit80 bbox + 8% pad
ys, xs = np.where(lit)
pad = 0.08
x0 = max(0, int(xs.min() - (xs.max()-xs.min())*pad)); x1 = min(W, int(xs.max() + (xs.max()-xs.min())*pad))
y0 = max(0, int(ys.min() - (ys.max()-ys.min())*pad)); y1 = min(H, int(ys.max() + (ys.max()-ys.min())*pad))
print(f'box: rows[{y0}:{y1}] cols[{x0}:{x1}]  ({y1-y0}x{x1-x0})')
reg = d[y0:y1, x0:x1]
rmax = reg.max(axis=2)
for name, thr in (('lit80', 80), ('px150', 150)):
    m = rmax > thr
    if m.sum() > 50:
        pxx = reg[m]; mm = pxx.mean(axis=0)
        print(f'  box {name} n={int(m.sum())} R={mm[0]:.1f} G={mm[1]:.1f} B={mm[2]:.1f}')
print(f'BOX {y0}:{y1}:{x0}:{x1}')
