#!/usr/bin/env python3
"""B1164 dev2: horizontal lit-80 profile of one frame in HOST units (fx_timing.py mask, tail baseline):
the fraction of pixels lit in each 0.1-host dx bin over up in [UP0, UP1] host heights.
usage: b1164_profile.py <label> <fmt> <frame> <tail_first> <tail_last> <crop> <feet> <host_px> <up0> <up1> [EXC x,y,w,h;...]"""
import sys, numpy as np, cv2
lab, fmt, f, t0, t1, crop, feet, hpx, u0, u1 = sys.argv[1:11]
exc = [tuple(map(int, e.split(','))) for e in (sys.argv[11].split(';') if len(sys.argv) > 11 else []) if e]
f, t0, t1 = int(f), int(t0), int(t1); hpx, u0, u1 = float(hpx), float(u0), float(u1)
x, y, w, h = map(int, crop.split(',')); fx, fy = map(int, feet.split(','))
load = lambda k: cv2.imread(fmt.format(f=k))[y:y + h, x:x + w].astype(np.int16)
base = np.median(np.stack([load(k) for k in range(t0, t1 + 1)]), axis=0).astype(np.int16)
lit = (load(f) - base).max(axis=2) > 80
X = np.arange(w)[None, :] + x + 0 * np.arange(h)[:, None]; Y = np.arange(h)[:, None] + y + 0 * X
ok = np.ones_like(lit)
for ex, ey, ew, eh in exc: ok &= ~((X >= ex) & (X < ex + ew) & (Y >= ey) & (Y < ey + eh))
dx = (X - fx) / hpx; up = (fy - Y) / hpx
sel = ok & (up >= u0) & (up <= u1)
print(f'# {lab} f{f} host {hpx:.0f}px feet {feet} up {u0}..{u1} exc {exc}')
print('dx_bin\tlit_frac\tpixels')
for b in np.arange(-2.0, 2.0, 0.1):
    m = sel & (dx >= b) & (dx < b + 0.1)
    if m.sum() > 50: print(f'{b:+.1f}\t{lit[m].mean():.3f}\t{int(m.sum())}')
