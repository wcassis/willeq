#!/usr/bin/env python3
"""B1164 dev2: per-SPRITE lit area of the landing spark field, host-normalised, same mask as fx_timing.py
(max-RGB rise > 80 over the TAIL median baseline) and the fx_sprite_roi.py sprite filter (8-connected,
>= 2 px, fill >= 0.25 of the bbox, aspect <= 2.2), restricted to the OUTSIDE band |dx| >= 0.6 host
(the spark field, off the column) inside the common window WIN (host heights).
env EXC='x,y,w,h;...' drops FULL-frame boxes (UI, the caster) from the band.
usage: b1164_sparks.py <label> <fmt> <f_first> <f_last> <tail_first> <tail_last> <crop> <feet> <host_px> <WIN>
Per frame and pooled: sprite count, median / p90 / mean lit area (1e-4 host^2), equivalent diameter
(host), mean peak rise, total sprite lit area; plus all lit80 outside (host^2)."""
import sys, numpy as np, cv2
from scipy import ndimage
lab, fmt, f0, f1, t0, t1, crop, feet, hpx, win = sys.argv[1:11]
f0, f1, t0, t1 = int(f0), int(f1), int(t0), int(t1); hpx = float(hpx)
x, y, w, h = map(int, crop.split(',')); fx, fy = map(int, feet.split(','))
a0, a1, b0, b1 = map(float, win.split(','))
TH = 80
def load(f):
    return cv2.imread(fmt.format(f=f))[y:y + h, x:x + w].astype(np.int16)
base = np.median(np.stack([load(f) for f in range(t0, t1 + 1)]), axis=0).astype(np.int16)
xx = np.arange(w)[None, :] + x + 0 * np.arange(h)[:, None]; yy = np.arange(h)[:, None] + y + 0 * xx
dx = (xx - fx) / hpx; up = (fy - yy) / hpx
import os
EXC = [tuple(map(int, e.split(','))) for e in os.environ.get('EXC', '').split(';') if e]  # FULL-frame x,y,w,h boxes
BAND = float(os.environ.get("BAND", "0.6"))
band = (np.abs(dx) >= BAND) & (dx >= a0) & (dx <= a1) & (up >= b0) & (up <= b1)
for ex, ey, ew, eh in EXC:
    band &= ~((xx >= ex) & (xx < ex + ew) & (yy >= ey) & (yy < ey + eh))
print(f'# EXC (full-frame boxes) {EXC}')
print(f'# {lab} crop {crop} feet {feet} host {hpx:.0f}px tail f{t0}..f{t1} WIN {win} band |dx|>={BAND} TH {TH}')
print('frame\tsprites\tmed_area_e4\tp90_area_e4\tmean_area_e4\tp90_diam_host\tmean_peak_rise\tsprite_lit_host2\tall_out80_host2\tbig_blobs\tbig_lit_host2')
pool_a, pool_r = [], []
for f in range(f0, f1 + 1):
    d = (load(f) - base).max(axis=2)
    m = (d > TH) & band
    L, n = ndimage.label(m, structure=np.ones((3, 3)))
    areas, peaks, big, bigpx = [], [], 0, 0
    for i, sl in enumerate(ndimage.find_objects(L), 1):
        blob = L[sl] == i; a = int(blob.sum()); hh, ww = sl[0].stop - sl[0].start, sl[1].stop - sl[1].start
        if a >= 2 and a >= 0.25 * hh * ww and max(hh, ww) <= 2.2 * min(hh, ww):
            areas.append(a); peaks.append(int(d[sl][blob].max()))
        elif a >= 2:
            big += 1; bigpx += a
    A = np.array(areas, float) / hpx**2 * 1e4
    pool_a += list(A); pool_r += peaks
    if len(A):
        print(f'{f}\t{len(A)}\t{np.median(A):.2f}\t{np.percentile(A, 90):.2f}\t{A.mean():.2f}\t'
              f'{2 * np.sqrt(np.percentile(A, 90) * 1e-4 / np.pi):.3f}\t{np.mean(peaks):.0f}\t{A.sum() * 1e-4:.3f}\t{m.sum() / hpx**2:.3f}\t{big}\t{bigpx / hpx**2:.3f}')
A = np.array(pool_a)
print(f'POOLED f{f0}..f{f1}: sprites {len(A)} ({len(A) / (f1 - f0 + 1):.1f}/frame) median {np.median(A):.2f} p90 {np.percentile(A, 90):.2f} '
      f'mean {A.mean():.2f} (1e-4 host^2) p90 diam {2 * np.sqrt(np.percentile(A, 90) * 1e-4 / np.pi):.3f} host mean peak rise {np.mean(pool_r):.0f}')
