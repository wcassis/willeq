#!/usr/bin/env python3
"""B1164: lit-80 fraction (fx_timing.py mask: max-RGB rise > 80 over the tail median) of ONE frame
on a host-unit grid: rows = up bands, cols = dx bands, each cell 0.25 host heights.
usage: b1164_grid.py <label> <fmt> <frame> <tail_first> <tail_last> <crop> <feet x,y> <host_px>"""
import sys, numpy as np, cv2
lab, fmt, f, t0, t1, crop, feet, hpx = sys.argv[1:9]
f, t0, t1, hpx = int(f), int(t0), int(t1), float(hpx)
x, y, w, h = map(int, crop.split(',')); fx, fy = map(int, feet.split(','))
load = lambda k: cv2.imread(fmt.format(f=k))[y:y + h, x:x + w].astype(np.int16)
base = np.median(np.stack([load(k) for k in range(t0, t1 + 1)]), axis=0).astype(np.int16)
lit = (load(f) - base).max(axis=2) > 80
X = (np.arange(w)[None, :] + x - fx) / hpx + 0 * np.arange(h)[:, None]
U = (fy - (np.arange(h)[:, None] + y)) / hpx + 0 * X
dxs = np.arange(-1.5, 1.5, 0.25); ups = np.arange(2.0, -0.25, -0.25)
print(f'# {lab} f{f} host {hpx:.0f}px feet {feet} crop {crop}: lit fraction per 0.25-host cell (- = outside crop); total lit {lit.sum()} px = {lit.sum() / hpx**2:.3f} host^2')
print('up\\dx ' + ' '.join(f'{d:+5.2f}' for d in dxs))
for u in ups:
    row = []
    for d in dxs:
        m = (X >= d) & (X < d + 0.25) & (U >= u) & (U < u + 0.25)
        row.append(f'{lit[m].mean():5.2f}' if m.sum() > 50 else '    -')
    print(f'{u:+5.2f} ' + ' '.join(row))
