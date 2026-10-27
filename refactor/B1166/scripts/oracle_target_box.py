#!/usr/bin/env python3
"""B1166: the r51 oracle target's own box (head top .. boot bottom, columns), by
DIFFERENCE against a target-free still at r51's camera, with the client's target
ring (the green selection disc drawn on the ground under a targeted npc) removed by
colour (a pixel whose green exceeds both red and blue by > 30 in the target still)
and the name label removed by keeping only the connected component that contains
the torso seed. usage: oracle_target_box.py <with_target.png> <target_free.png> <out_mask.png> [seed_x seed_y]"""
import sys, cv2, numpy as np
a = cv2.imread(sys.argv[1]).astype(int); b = cv2.imread(sys.argv[2]).astype(int)
sx, sy = (int(sys.argv[4]), int(sys.argv[5])) if len(sys.argv) > 5 else (940, 450)
thr = int(__import__("os").environ.get("THR", "40"))
d = np.abs(a - b).max(axis=2) > thr
B, G, R = a[..., 0], a[..., 1], a[..., 2]
ring = (G - R > 30) & (G - B > 30)
m = (d & ~ring).astype(np.uint8)
m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, np.ones((3, 3), np.uint8))
n, lab, st, cen = cv2.connectedComponentsWithStats(m, connectivity=8)
k = lab[sy, sx]
assert k != 0, 'seed not on the mask'
comp = lab == k
ys, xs = np.nonzero(comp)
# B1164's row rule (minpx 3): a row belongs to the figure when it holds >= 3
# component pixels; top = the first such row, feet = the LAST such row (the ring,
# removed above, starts below it).
MINPX = 3
rcnt = np.bincount(ys)
rows_ok = [y for y in range(len(rcnt)) if rcnt[y] >= MINPX]
top, bot = rows_ok[0], rows_ok[-1]
print(f'{sys.argv[1]} - {sys.argv[2]}: thr {thr}, ring removed ({int((d & ring).sum())} px), component at seed ({sx},{sy}): {comp.sum()} px')
print(f'minpx {MINPX}: rows {top}..{bot} height {bot - top + 1} px; cols {xs.min()}..{xs.max()}; centroid column {xs.mean():.1f}')
lo = (ys >= bot - 10) & (ys <= bot)
print(f'feet rows {bot - 10}..{bot}: column centroid {xs[lo].mean():.1f}, cols {xs[lo].min()}..{xs[lo].max()}')
print('# row counts ' + ' '.join(f'{y}:{rcnt[y]}' for y in range(ys.min(), ys.max() + 1)))
cv2.imwrite(sys.argv[3], comp.astype(np.uint8) * 255)
