#!/usr/bin/env python3
"""B1164 dev2: split the LANDING lit-80 mask into host-relative bands, same mask as fx_timing.py
(max-RGB rise > 80 over the TAIL median baseline), normalised by the HOST (landing target) height.
env WIN=dxmin,dxmax,upmin,upmax (host heights) restricts the mask to a common window.
usage: b1164_regions.py <label> <frame_glob_fmt> <f_first> <f_last> <tail_first> <tail_last>
                        <crop x,y,w,h> <feet x,y (full frame)> <host_px> <norm_px> [exclude x,y,w,h ...]
frame_glob_fmt: python format with {f} for the frame index (full-frame PNGs).
Prints, per frame: lit80 / norm_px^2 (= the gate's cov when norm_px is the gate's model px), lit80 in
host-heights^2, and the split |dx|<0.5 host (column) vs |dx|>=0.5 (outside), and the rise histogram."""
import sys, numpy as np, cv2
lab, fmt, f0, f1, t0, t1, crop, feet, hpx, npx = sys.argv[1:11]
f0, f1, t0, t1 = int(f0), int(f1), int(t0), int(t1); hpx = float(hpx); npx = float(npx)
x, y, w, h = map(int, crop.split(',')); fx, fy = map(int, feet.split(','))
exc = [tuple(map(int, e.split(','))) for e in sys.argv[11:]]
def load(f):
    return cv2.imread(fmt.format(f=f))[y:y + h, x:x + w].astype(np.int16)
base = np.median(np.stack([load(f) for f in range(t0, t1 + 1)]), axis=0).astype(np.int16)
xx = np.arange(w)[None, :] + x; yy = np.arange(h)[:, None] + y
dx = (xx - fx) / hpx + 0 * yy; up = (fy - yy) / hpx + 0 * xx
import os
keep = np.ones((h, w), bool)
WIN = os.environ.get('WIN')  # dxmin,dxmax,upmin,upmax in HOST heights: a common window across geometries
if WIN:
    a0, a1, b0, b1 = map(float, WIN.split(','))
    keep &= (dx >= a0) & (dx <= a1) & (up >= b0) & (up <= b1)
for ex, ey, ew, eh in exc:  # exclusion boxes in CROP coordinates (anchor.py convention)
    keep[ey:ey + eh, ex:ex + ew] = False
print(f'# {lab} crop {crop} feet {feet} host {hpx:.0f}px norm {npx:.0f}px tail f{t0}..f{t1} exclude {exc} WIN {WIN}')
print('frame\tlit80\tcov_norm\tcov_host\tcol80_host\tout80_host\tlit30_host\tmean_rise_lit80\tfrac_rise>=200_of_lit80')
for f in range(f0, f1 + 1):
    d = (load(f) - base).max(axis=2)
    m80 = (d > 80) & keep; m30 = (d > 30) & keep
    col = m80 & (np.abs(dx) < 0.5)
    n = int(m80.sum())
    print(f'{f}\t{n}\t{n / npx**2:.3f}\t{n / hpx**2:.3f}\t{col.sum() / hpx**2:.3f}\t{(n - col.sum()) / hpx**2:.3f}\t'
          f'{m30.sum() / hpx**2:.3f}\t{d[m80].mean() if n else 0:.0f}\t{(d[m80] >= 200).mean() if n else 0:.3f}')
