#!/usr/bin/env python3
"""B1164 (dev4): threshold sensitivity of b1164_hostpx.py's difference height.
For one (with, without) still pair and a column band x0..x1, prints for each threshold in THRS
the per-row count of pixels whose max-RGB difference exceeds the threshold over rows y0..y1, the
first/last row with >= minpx such pixels, and the height. Also prints the per-row max difference
inside the band (the boot's contrast) and, with --probe x,y, the BGR of the with-still at that
pixel (e.g. the Main Chat window's top border), and with --darkb N the per-row count of band
pixels in the with-still whose BLUE channel is < N (r51's boots are BGR ~(16,33,78) on ground
with B 33: a colour test independent of the difference threshold).
usage: b1164_hostpx_sweep.py <with.png> <without.png> <x0> <x1> <y0> <y1> [thr,thr,..=40,20,15]
       [minpx=3] [--probe x,y ...] [--darkb N]"""
import sys, numpy as np, cv2
probes, darkb, pos = [], None, []
argv = sys.argv[1:]
i = 0
while i < len(argv):
    if argv[i] == '--probe':
        probes.append(tuple(map(int, argv[i + 1].split(',')))); i += 2
    elif argv[i] == '--darkb':
        darkb = int(argv[i + 1]); i += 2
    else:
        pos.append(argv[i]); i += 1
wp, wo = pos[0], pos[1]
x0, x1, y0, y1 = map(int, pos[2:6])
thrs = [int(t) for t in pos[6].split(',')] if len(pos) > 6 else [40, 20, 15]
mp = int(pos[7]) if len(pos) > 7 else 3
a = cv2.imread(wp).astype(np.int16); b = cv2.imread(wo).astype(np.int16)
d = np.abs(a - b).max(axis=2)
print(f'# {wp} - {wo} band x {x0}..{x1} rows {y0}..{y1} minpx {mp}')
print('# max diff per row', ' '.join(f'{y}:{d[y, x0:x1 + 1].max()}' for y in range(y0, y1 + 1)))
for thr in thrs:
    cnt = (d > thr)[:, x0:x1 + 1].sum(axis=1)
    rows = [y for y in range(y0, y1 + 1) if cnt[y] >= mp]
    print(f'# thr {thr} row counts', ' '.join(f'{y}:{cnt[y]}' for y in range(y0, y1 + 1)))
    if rows:
        print(f'thr {thr}: top {rows[0]} bottom {rows[-1]} height {rows[-1] - rows[0] + 1} px')
    else:
        print(f'thr {thr}: no rows')
if darkb is not None:
    cb = (a[:, x0:x1 + 1, 0] < darkb).sum(axis=1)
    rows = [y for y in range(y0, y1 + 1) if cb[y] >= mp]
    print(f'# B<{darkb} row counts', ' '.join(f'{y}:{cb[y]}' for y in range(y0, y1 + 1)))
    print(f'B<{darkb}: ' + (f'top {rows[0]} bottom {rows[-1]} height {rows[-1] - rows[0] + 1} px' if rows else 'no rows'))
for (px, py) in probes:
    print(f'probe ({px},{py}) with BGR {tuple(int(v) for v in a[py, px])} without BGR '
          f'{tuple(int(v) for v in b[py, px])}')
